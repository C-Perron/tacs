#ifndef TACS_INTEGRATOR_H
#define TACS_INTEGRATOR_H

#include "TACSObject.h"
#include "TACSAssembler.h"
#include "KSM.h"

/*
  Base class for integration schemes. This base class contains common
  methods and variables pertaining to the integration schemes used in
  TACS.

  Copyright (c) 2010-2016 Graeme Kennedy. All rights reserved. 
*/

class TacsIntegrator : public TACSObject {
 public:
  // Parent class constructor for integration schemes
  //-------------------------------------------------
  TacsIntegrator( TACSAssembler *_tacs, 
                  double _tinit, double _tfinal, 
                  int _num_steps_per_sec );

  // Destructor
  //-----------
  ~TacsIntegrator();
  
  // Method to solve the non-linear system using Newton's method
  //------------------------------------------------------------
  void newtonSolve( double alpha, double beta, double gamma,
                    double t, BVec *q, BVec *qdot, 
                    BVec *qddot );

  // Call this function after integrating to write the solution to file
  //-------------------------------------------------------------------
  void writeSolution( const char *filename );

  // Call this function after integrating to write the solution to f5
  // file
  //-------------------------------------------------------------------
  void writeSolutionToF5();

  // Pure virtual function that the derived classes must override/implement
  //-----------------------------------------------------------------------
  virtual void integrate() = 0;

  // Pure virtual function for solving the adjoint variables
  //--------------------------------------------------------
  virtual void adjointSolve() = 0;

  // Setters
  //--------
  void setMaxNewtonIters( int _max_newton_iters ){
    max_newton_iters = _max_newton_iters;
  }
  void setRelTol( double _rtol ){ rtol = _rtol; }
  void setAbsTol( double _atol ){ atol = _atol; }
  void setPrintLevel( int _print_level ){ 
    print_level = _print_level; 
  }

  // Set the objective/constraint function of interest and increment the counter
  //----------------------------------------------------------------------------
  void setFunction( TACSFunction **_func, int _num_funcs ){
    // Increase the reference counts to the functions
    for ( int i = 0; i < _num_funcs; i++ ){
      _func[i]->incref();
    }

    // Delete the references to the old functions
    if (func){
      for ( int i = 0; i < num_func; i++ ){
	func[i]->decref();	
      }
      delete [] func;
    }
    
    num_func = _num_funcs;
    func = new TACSFunction*[ num_func ];
    memcpy(func, _func, num_func*sizeof(TACSFunction*));    
  }
  
 protected:
  // Instance of TACS
  TACSAssembler *tacs; 
  
  // Store the history of states over time
  BVec **q, **qdot, **qddot;
  double *time;

  // Number of state variables
  int num_state_vars;
  
  // Class variables used to manage/monitor time marching in
  // integration schemes
  int num_time_steps, num_steps_per_sec, current_time_step, current_stage; 
  double h, tinit, tfinal;

  // Print and output options
  int print_level;

  // Variables controlling the nonlinear solution
  int max_newton_iters;
  double atol, rtol;
  
  // Frequency of Jacobian recomputation during nonlinear solve
  int jac_comp_freq;

  // Matrices and vectors for the nonlinear solution
  BVec *res, *update;  // Residual and Newton update
  FEMat *D;            // Matrix associated with Preconditioner
  TACSMat *mat;        // Jacobian matrix
  TACSPc *pc;          // Preconditioner
  TACSKsm *ksm;        // KSM solver

  // The objective and contraint functions
  TACSFunction **func;
  int num_func;
};

/*
  DIRK integration scheme for TACS
*/
class TacsDIRKIntegrator : public TacsIntegrator {
 public:
  // Constructor for DIRK object
  //----------------------------
  TacsDIRKIntegrator( TACSAssembler * _tacs, 
                      double _tinit, double _tfinal, int _num_steps_per_sec, 
                      int num_stages );
  
  // Desctructor
  //------------
  ~TacsDIRKIntegrator();
  
  // function to call to integrate in time
  //--------------------------------------
  void integrate();

  // solve for adjoint variables
  //----------------------------
  void adjointSolve();
  
 private:
  // the number of stage in RK scheme
  int num_stages;
  
  // the order of accuracy of the scheme
  int order;
  
  // variables for Butcher tableau
  double *A, *B, *C;
  
  // stage values (computed at each time stage for each time step)
  double *tS;
  BVec **qS, **qdotS, **qddotS;

  // Adjoint variables
  BVec **psi;

  // Functions related to Butcher Tableau
  //-------------------------------------
  void setupButcherTableau();
  void checkButcherTableau();

  // Returns the starting index of Butcher tableau
  //----------------------------------------------
  int getIdx(int stageNum);

  // Compute the stages stage values 
  //---------------------------------
  void computeStageValues();

  // Advance the time and states to next step
  //-----------------------------------------
  void timeMarch(double *time, BVec **q, BVec **qdot, BVec **qddot);

  // Setup the right hand side of the adjoint equation
  //------------------------------------------
  void setupAdjointRHS(BVec *res, int func_num);
};

/*
  BDF integration scheme for TACS which extends TacsIntegrator
*/

class TacsBDFIntegrator : public TacsIntegrator {

 public:
  
  // Constructor for BDF object
  //---------------------------
  TacsBDFIntegrator(TACSAssembler * _tacs, 
		    double _tinit, double _tfinal, int _num_steps_per_sec, 
		    int max_bdf_order);
  
  // Destructor for BDF object
  //--------------------------
  ~TacsBDFIntegrator();
  
  // Function that integrates forward in time
  //-----------------------------------------
  void integrate();

  // solve for adjoint variables
  //----------------------------
  void adjointSolve();
  
 private:  

  // Maximum order of the BDF integration scheme
  int max_bdf_order;

  // Number of first and second order BDF coefficients
  int nbdf, nbddf;

  // Class variable to store BDF coefficients
  double bdf_coeff[4], bddf_coeff[9];

  // Adjoint variables
  BVec **psi;

  // Retrieve the first order BDF coefficients
  //------------------------------------------
  int getBDFCoeff(double bdf[], int order );

  // Retrieve the second order BDF coefficients
  //------------------------------------------
  void get2ndBDFCoeff(const int k, double bdf[], int *nbdf,
		      double bddf[], int *nbddf,
		      const int max_order);
  
  // approximate derivatives using BDF stencil
  //------------------------------------------
  void approxStates(BVec **q, BVec **qdot, BVec **qddot);

  // Setup the right hand side of the adjoint equation
  //------------------------------------------
  void setupAdjointRHS(BVec *res, int func_num);
};

#endif

