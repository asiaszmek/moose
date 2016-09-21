/**********************************************************************
 ** This program is part of 'MOOSE', the
 ** Multiscale Object Oriented Simulation Environment.
 **           copyright (C) 2003-2008
 **           Upinder S. Bhalla, Niraj Dudani and NCBS
 ** It is made available under the terms of the
 ** GNU Lesser General Public License version 2.1
 ** See the file COPYING.LIB for the full notice.
 **********************************************************************/

#include "header.h"
#include "DifShell.h"
#include "DifShellBase.h"

const double DifShell::EPSILON = 1.0e-10;
const double DifShell::F = 96485.3415; /* C / mol like in genesis */

const Cinfo* DifShell::initCinfo()
{
    
  
  static string doc[] =
    {
      "Name", "DifShell",
      "Author", "Niraj Dudani. Ported to async13 by Subhasis Ray.",
      "Description", "DifShell object: Models diffusion of an ion (typically calcium) within an "
      "electric compartment. A DifShell is an iso-concentration region with respect to "
      "the ion. Adjoining DifShells exchange flux of this ion, and also keep track of "
      "changes in concentration due to pumping, buffering and channel currents, by "
      "talking to the appropriate objects.",
    };
  static Dinfo< DifShell > dinfo;
  static Cinfo difShellCinfo(
			     "DifShell",
			     Neutral::initCinfo(),
			     0,
			     0,
			     &dinfo,
			     doc,
			     sizeof( doc ) / sizeof( string ));

  return &difShellCinfo;
}

//Cinfo *object*  corresponding to the class.
static const Cinfo* difShellCinfo = DifShell::initCinfo();

////////////////////////////////////////////////////////////////////////////////
// Class functions
////////////////////////////////////////////////////////////////////////////////

/// Faraday's constant (Coulomb / Mole)


DifShell::DifShell() :
  dCbyDt_( 0.0 ),
  Cmultiplier_(0.0),
  C_( 0.0 ),
  Ceq_( 0.0 ),
  D_( 0.0 ),
  valence_( 0.0 ),
  leak_( 0.0 )
{ ; }

////////////////////////////////////////////////////////////////////////////////
// Field access functions
////////////////////////////////////////////////////////////////////////////////
/// C is a read-only field


void DifShell::vSetC( const Eref& e, double C)
{
  if ( C < 0.0 ) {
    cerr << "Error: DifShell: C cannot be negative!\n";
    return;
  }
	
  C_ = C;
}
double DifShell::vGetC(const Eref& e) const
{
  return C_;
}

void DifShell::vSetCeq( const Eref& e, double Ceq )
{
  if ( Ceq < 0.0 ) {
    cerr << "Error: DifShell: Ceq cannot be negative!\n";
    return;
  }
	
  Ceq_ = Ceq;
}

double DifShell::vGetCeq(const Eref& e ) const
{
  return Ceq_;
}

void DifShell::vSetD(const Eref& e, double D )
{
  if ( D < 0.0 ) {
    cerr << "Error: DifShell: D cannot be negative!\n";
    return;
  }
	
  D_ = D;
}

double DifShell::vGetD(const Eref& e ) const
{
  return D_;
}

void DifShell::vSetValence(const Eref& e, double valence )
{
  if ( valence < 0.0 ) {
    cerr << "Error: DifShell: valence cannot be negative!\n";
    return;
  }
	
  valence_ = valence;
}

double DifShell::vGetValence(const Eref& e ) const 
{
  return valence_;
}

void DifShell::vSetLeak(const Eref& e, double leak )
{
  leak_ = leak;
}

double DifShell::vGetLeak(const Eref& e ) const
{
  return leak_;
}

////////////////////////////////////////////////////////////////////////////////
// Local dest functions
////////////////////////////////////////////////////////////////////////////////
double DifShell::integrate( double state, double dt, double A, double B )
{
	if ( B > EPSILON ) {
		double x = exp( -B * dt );
		return state * x + ( A / B ) * ( 1 - x );
	}
	return state + A * dt ;
}

void DifShell::vReinit( const Eref& e, ProcPtr p )
{
  dCbyDt_ = leak_;
  Cmultiplier_ = 0;
  C_ = Ceq_;
  const double dOut = diameter_;
  const double dIn = diameter_ - thickness_;
	
  switch ( shapeMode_ )
    {
      /*
       * Onion Shell
       */
    case 0:
      if ( length_ == 0.0 ) { // Spherical shell
	volume_ = ( M_PI / 6.0 ) * ( dOut * dOut * dOut - dIn * dIn * dIn );
	outerArea_ = M_PI * dOut * dOut;
	innerArea_ = M_PI * dIn * dIn;
      } else { // Cylindrical shell
	volume_ = ( M_PI * length_ / 4.0 ) * ( dOut * dOut - dIn * dIn );
	outerArea_ = M_PI * dOut * length_;
	innerArea_ = M_PI * dIn * length_;
      }
		
      break;
	
      /*
       * Cylindrical Slice
       */
    case 1:
      volume_ = M_PI * diameter_ * diameter_ * thickness_ / 4.0;
      outerArea_ = M_PI * diameter_ * diameter_ / 4.0;
      innerArea_ = outerArea_;
      break;
	
      /*
       * User defined
       */
    case 3:
      // Nothing to be done here. Volume and inner-, outer areas specified by
      // user.
      break;
	
    default:
      assert( 0 );
    }
}

void DifShell::vProcess( const Eref & e, ProcPtr p )
{
  /**
   * Send ion concentration and thickness to adjacent DifShells. They will
   * then compute their incoming fluxes.
   */
  
  innerDifSourceOut()->send( e, C_, thickness_ );
  outerDifSourceOut()->send( e, C_, thickness_ );
  C_ = integrate(C_,p->dt,dCbyDt_,Cmultiplier_);
    
  /**
   * Send ion concentration to ion buffers. They will send back information on
   * the reaction (forward / backward rates ; free / bound buffer concentration)
   * immediately, which this DifShell will use to find amount of ion captured
   * or released in the current time-step.
   */
  concentrationOut()->send( e, C_ );
  dCbyDt_ = leak_;
  Cmultiplier_ = 0;
}
void DifShell::vBuffer(const Eref& e,
			   double kf,
			   double kb,
			   double bFree,
			   double bBound )
{
  dCbyDt_ += kb * bBound;
  Cmultiplier_ += kf * bFree;
}

void DifShell::vFluxFromOut(const Eref& e, double outerC, double outerThickness )
{
  double diff =2.* ( D_ / volume_ ) * ( outerArea_ / (outerThickness + thickness_) );
  //influx from outer shell
  /**
   * We could pre-compute ( D / Volume ), but let us leave the optimizations
   * for the solver.
   */
  dCbyDt_ +=  diff * outerC;
  Cmultiplier_ += diff ;
}

void DifShell::vFluxFromIn(const Eref& e, double innerC, double innerThickness )
{
  //influx from inner shell
  //double dx = ( innerThickness + thickness_ ) / 2.0;
  double diff = 2.*( D_ / volume_ ) * ( innerArea_ / (innerThickness + thickness_) );

  dCbyDt_ +=  diff *  innerC ;
  Cmultiplier_ += diff ;
}

void DifShell::vInflux(const Eref& e,	double I )
{
  /**
   * I: Amperes
   * F_: Faraday's constant: Coulomb / mole
   * valence_: charge on ion: dimensionless
   */
  dCbyDt_ += I / ( F * valence_ * volume_ );
}

/**
 * Same as influx, except subtracting.
 */
void DifShell::vOutflux(const Eref& e, double I )
{
  dCbyDt_ -= I / ( F * valence_ * volume_ );
}

void DifShell::vFInflux(const Eref& e, double I, double fraction )
{
  dCbyDt_ += fraction * I / ( F * valence_ * volume_ );
}

void DifShell::vFOutflux(const Eref& e, double I, double fraction )
{
  dCbyDt_ -= fraction * I / ( F * valence_ * volume_ );
}

void DifShell::vStoreInflux(const Eref& e, double flux )
{
  dCbyDt_ += flux / volume_;
}

void DifShell::vStoreOutflux(const Eref& e, double flux )
{
  dCbyDt_ -= flux / volume_;
}

void DifShell::vTauPump(const Eref& e, double kP, double Ceq )
{
  //dCbyDt_ += -kP * ( C_ - Ceq );
  dCbyDt_ += kP*Ceq;
  Cmultiplier_ -= kP;
}

/**
 * Same as tauPump, except that we use the v value of Ceq.
 */
void DifShell::vEqTauPump(const Eref& e, double kP )
{
  dCbyDt_ += kP*Ceq_;
  Cmultiplier_ -= kP;
}

void DifShell::vMMPump(const Eref& e, double vMax, double Kd )
{
  Cmultiplier_ += -( vMax / volume_ )  / ( C_ + Kd ) ;
}

void DifShell::vHillPump(const Eref& e, double vMax, double Kd, unsigned int hill )
{
  //This needs fixing
  double ch;
  switch( hill )
    {
    case 0:
      ch = 1.0;
      break;
    case 1:
      ch = C_;
      break;
    case 2:
      ch = C_ * C_;
      break;
    case 3:
      ch = C_ * C_ * C_;
      break;
    case 4:
      ch = C_ * C_;
      ch = ch * ch;
      break;
    default:
      ch = pow( C_, static_cast< double >( hill ) );
    };
	
  dCbyDt_ += -( vMax / volume_ ) * ( ch / ( ch + Kd ) );
}

////////////////////////////////////////////////////////////////////////////////


