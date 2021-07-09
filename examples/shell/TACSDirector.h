#ifndef TACS_DIRECTOR_H
#define TACS_DIRECTOR_H

#include "TACSElementAlgebra.h"
#include "TACSElementVerification.h"

/*
  The director class.

  Given a reference vector, t, from the element geometry, the director
  computes the exact or approximate rate of change of the displacement
  through the thickness.
*/
class TACSLinearizedRotation {
 public:
  static const int NUM_PARAMETERS = 3;

  /**
    Compute the rotation matrices at each node

    @param vars The full variable vector
    @param C The rotation matrices at each point
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void computeRotationMat( const TacsScalar vars[],
                                  TacsScalar C[] ){
    const TacsScalar *q = &vars[offset];
    for ( int i = 0; i < num_nodes; i++ ){
      // C = I - q^{x}
      setMatSkew(-1.0, q, C);
      C[0] = C[4] = C[8] = 1.0;

      C += 9;
      q += vars_per_node;
    }
  }

  /*
    Compute the derivative of the rotation matrices at each node

    @param vars The full variable vector
    @param varsd The full variable vector
    @param C The rotation matrices at each point
    @param Cd The rotation matrices at each point
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void computeRotationMatDeriv( const TacsScalar vars[],
                                       const TacsScalar varsd[],
                                       TacsScalar C[],
                                       TacsScalar Cd[] ){
    const TacsScalar *q = &vars[offset];
    const TacsScalar *qd = &varsd[offset];
    for ( int i = 0; i < num_nodes; i++ ){
      // C = I - q^{x}
      setMatSkew(-1.0, q, C);
      C[0] = C[4] = C[8] = 1.0;

      // Cd = - qd^{x}
      setMatSkew(-1.0, qd, Cd);

      C += 9;
      Cd += 9;

      q += vars_per_node;
      qd += vars_per_node;
    }
  }

  /*
    Add the residual rotation matrix to the output

    This code adds the contribution to the residual via the derivative

    d(tr(dC^{T}C(q)))/dq_{i} = d(tr(dC^{T}*(I - q^{x})))/dq_{i}

    @param vars The full variable vector
    @param dC The derivative w.r.t. the rotation matrix
    @param res The residual array
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void addRotationMatResidual( const TacsScalar vars[],
                                      const TacsScalar dC[],
                                      TacsScalar res[] ){
    TacsScalar *r = &res[offset];

    for ( int i = 0; i < num_nodes; i++ ){
      r[0] += -(dC[7] - dC[5]);
      r[1] += -(dC[2] - dC[6]);
      r[2] += -(dC[3] - dC[1]);

      r += vars_per_node;
      dC += 9;
    }
  }

  /*
    Add the
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void addRotationMatJacobian( const TacsScalar vars[],
                                      const TacsScalar d2C[],
                                      TacsScalar mat[] ){
    const int size = vars_per_node*num_nodes;
    const int csize = 9*num_nodes;

    for ( int i = 0; i < num_nodes; i++ ){
      TacsScalar *m = &mat[offset*(size + 1)];

      for ( int j = 0; j < num_nodes; j++ ){
        // r[0] += -(dC[7] - dC[5]);
        // r[1] += -(dC[2] - dC[6]);
        // r[2] += -(dC[3] - dC[1]);
        // r += vars_per_node;
        // dC += 9;

        // Add the non-zero entries
        // m[0] += d2C[];



        m += vars_per_node;

        d2C += 9;
      }

      mat += vars_per_node*size;
      d2C += 8*csize;
    }
  }

  /**
    Compute the director and rates at all nodes.

    d = Q(q)*t = (C(q)^{T} - I)*t
    ddot = d/dt(Q(q))*t

    @param vars The full variable vector
    @param dvars The first time derivative of the variables
    @param ddvars The second derivatives of the variables
    @param t The reference directions
    @param d The director values
    @param ddot The first time derivative of the director
    @param dddot The second time derivative of the director
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void computeDirectorRates( const TacsScalar vars[],
                                    const TacsScalar dvars[],
                                    const TacsScalar t[],
                                    TacsScalar d[],
                                    TacsScalar ddot[] ){
    const TacsScalar *q = &vars[offset];
    const TacsScalar *qdot = &dvars[offset];
    for ( int i = 0; i < num_nodes; i++ ){
      crossProduct(q, t, d);
      crossProduct(qdot, t, ddot);

      t += 3;
      d += 3;
      ddot += 3;

      q += vars_per_node;
      qdot += vars_per_node;
    }
  }

  /**
    Compute the director and rates at all nodes.

    d = Q(q)*t = (C(q)^{T} - I)*t
    ddot = d/dt(Q(q))*t
    dddot = d^2/dt^2(Q(q))*t

    @param vars The full variable vector
    @param dvars The first time derivative of the variables
    @param ddvars The second derivatives of the variables
    @param t The reference directions
    @param d The director values
    @param ddot The first time derivative of the director
    @param dddot The second time derivative of the director
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void computeDirectorRates( const TacsScalar vars[],
                                    const TacsScalar dvars[],
                                    const TacsScalar ddvars[],
                                    const TacsScalar t[],
                                    TacsScalar d[],
                                    TacsScalar ddot[],
                                    TacsScalar dddot[] ){
    const TacsScalar *q = &vars[offset];
    const TacsScalar *qdot = &dvars[offset];
    const TacsScalar *qddot = &ddvars[offset];
    for ( int i = 0; i < num_nodes; i++ ){
      crossProduct(q, t, d);
      crossProduct(qdot, t, ddot);
      crossProduct(qddot, t, dddot);

      t += 3;
      d += 3;
      ddot += 3;
      dddot += 3;

      q += vars_per_node;
      qdot += vars_per_node;
      qddot += vars_per_node;
    }
  }

  /**
    Compute the director and rates at all nodes and the derivative.

    d = Q(q)*t = (C(q)^{T} - I)*t
    ddot = d/dt(Q(q))*t
    dddot = d^2/dt^2(Q(q))*t

    @param vars The full variable vector
    @param dvars The first time derivative of the variables
    @param ddvars The second derivatives of the variables
    @param varsd The full variable vector derivative
    @param t The reference directions
    @param C The rotation matrices at each point
    @param d The director values
    @param ddot The first time derivative of the director
    @param dddot The second time derivative of the director
    @param Cd The derivative of the rotation matrices at each point
    @param dd The derivator of the director values
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void computeDirectorRatesDeriv( const TacsScalar vars[],
                                         const TacsScalar dvars[],
                                         const TacsScalar ddvars[],
                                         const TacsScalar varsd[],
                                         const TacsScalar t[],
                                         TacsScalar d[],
                                         TacsScalar ddot[],
                                         TacsScalar dddot[],
                                         TacsScalar dd[] ){
    const TacsScalar *q = &vars[offset];
    const TacsScalar *qdot = &dvars[offset];
    const TacsScalar *qddot = &ddvars[offset];
    const TacsScalar *qd = &varsd[offset];
    for ( int i = 0; i < num_nodes; i++ ){
      crossProduct(q, t, d);
      crossProduct(qdot, t, ddot);
      crossProduct(qddot, t, dddot);

      // Cd = - qd^{x}
      crossProduct(qd, t, dd);

      t += 3;
      d += 3;
      dd += 3;
      ddot += 3;
      dddot += 3;

      q += vars_per_node;
      qdot += vars_per_node;
      qddot += vars_per_node;
      qd += vars_per_node;
    }
  }

  /**
    Given the derivatives of the kinetic energy expression with
    respect to time, add the contributions to the derivative of the

    Given the partial derivatives of the Lagrangian with respect to the
    director and the time derivative of the vector, compute

    dTdot = d/dt(dT/d(dot{d}))
    dT = dT/d(dot{d})
    dd = -dL/dd

    In general, the residual contribution is:

    res +=
    dTdot*d(dot{d})/d(dot{q}) +
    dT/d(dot{d})*d/dt(dot{d})/d(dot{q}) +
    dd*d(d)/d(q)

    For the linearized rotation director these expressions are:

    d = q^{x} t
    dot{d} = - t^{x} \dot{q}
    d(dot{d})/d(dot{q}) = - t^{x}
    d/dt(d(dot{d})/d(dot{q})) = 0

    @param vars The full variable vector
    @param dvars The first time derivative of the variables
    @param ddvars The second derivatives of the variables
    @param t The normal direction
    @param dTdot Time deriv. of the deriv. of the kinetic energy w.r.t. d
    @param dT The derivative of the kinetic energy w.r.t. director
    @param dd The contribution from the derivative of the director
    @param res The output residual
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void addDirectorResidual( const TacsScalar vars[],
                                   const TacsScalar dvars[],
                                   const TacsScalar ddvars[],
                                   const TacsScalar t[],
                                   const TacsScalar dTdot[],
                                   const TacsScalar dT[],
                                   const TacsScalar dd[],
                                   TacsScalar res[] ){
    TacsScalar *r = &res[offset];

    for ( int i = 0; i < num_nodes; i++ ){
      crossProductAdd(1.0, t, dd, r);
      crossProductAdd(1.0, t, dTdot, r);

      r += vars_per_node;
      dd += 3;
      dTdot += 3;
      t += 3;
    }
  }

  /*
    Add terms from the Jacobian
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void addDirectorJacobian( const TacsScalar vars[],
                                   const TacsScalar dvars[],
                                   const TacsScalar ddvars[],
                                   const TacsScalar t[],
                                   const TacsScalar d2d[],
                                   const TacsScalar d2du[],
                                   TacsScalar mat[] ){
    // Add the derivative due to and d2d
    const int dsize = 3*num_nodes;
    const int nvars = vars_per_node*num_nodes;

    // d = crossProduct(q, t, d)
    const TacsScalar *ti = t;
    for ( int i = 0; i < num_nodes; i++, ti += 3 ){
      TacsScalar *jac1 = &mat[(offset + vars_per_node*i)*nvars + 3];
      TacsScalar *jac2 = &mat[(offset + vars_per_node*i + 1)*nvars + 3];
      TacsScalar *jac3 = &mat[(offset + vars_per_node*i + 2)*nvars + 3];

      const TacsScalar *tj = t;
      for ( int j = 0; j < num_nodes; j++, tj += 3 ){
        // Add the derivative
        TacsScalar d[9];
        d[0] = d2d[0];
        d[1] = d2d[1];
        d[2] = d2d[2];

        d[3] = d2d[dsize];
        d[4] = d2d[dsize+1];
        d[5] = d2d[dsize+2];

        d[6] = d2d[2*dsize];
        d[7] = d2d[2*dsize+1];
        d[8] = d2d[2*dsize+2];

        TacsScalar tmp[9];
        mat3x3SkewMatSkewTransform(ti, d, tj, tmp);

        jac1[0] -= tmp[0];
        jac1[1] -= tmp[1];
        jac1[2] -= tmp[2];

        jac2[0] -= tmp[3];
        jac2[1] -= tmp[4];
        jac2[2] -= tmp[5];

        jac3[0] -= tmp[6];
        jac3[1] -= tmp[7];
        jac3[2] -= tmp[8];

        jac1 += vars_per_node;
        jac2 += vars_per_node;
        jac3 += vars_per_node;
        d2d += 3;
      }

      d2d += 2*dsize;
    }

    for ( int i = 0; i < num_nodes; i++ ){
      for ( int j = 0; j < num_nodes; j++ ){
        // Add the derivative
        TacsScalar d[9];
        d[0] = d2du[0];
        d[1] = d2du[1];
        d[2] = d2du[2];

        d[3] = d2du[dsize];
        d[4] = d2du[dsize+1];
        d[5] = d2du[dsize+2];

        d[6] = d2du[2*dsize];
        d[7] = d2du[2*dsize+1];
        d[8] = d2du[2*dsize+2];

        TacsScalar tmp[9];
        mat3x3SkewMatTransform(&t[3*i], d, tmp);

        for ( int ii = 0; ii < 3; ii++ ){
          for ( int jj = 0; jj < 3; jj++ ){
            int index =
              (vars_per_node*i + ii + offset)*nvars +
              vars_per_node*j + jj;

            mat[index] += tmp[3*ii + jj];
          }
        }

        for ( int ii = 0; ii < 3; ii++ ){
          for ( int jj = 0; jj < 3; jj++ ){
            int index =
              (vars_per_node*j + jj)*nvars +
              vars_per_node*i + ii + offset;

            mat[index] += tmp[3*ii + jj];
          }
        }

        d2du += 3;
      }

      d2du += 2*dsize;
    }
  }
};

/*
  A quadratic approximation of the director

  The director matrix is given as

  C(q) = 1 - q^{x} + 0.5*q^{x}q^{x}
  .    = 1 - q^{x} + 0.5*(qq^{T} - q^{T}q*1)
  .    = 1*(1 - 0.5*q^{T}q) - q^{x} + 0.5*q*q^{T}
*/
class TACSQuadraticRotation {
 public:
  static const int NUM_PARAMETERS = 3;

  /*
    Compute the rotation matrices at each node

    @param vars The full variable vector
    @param C The rotation matrices at each point
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void computeRotationMat( const TacsScalar vars[],
                                  TacsScalar C[] ){
    const TacsScalar *q = &vars[offset];
    for ( int i = 0; i < num_nodes; i++ ){
      TacsScalar qTq = vec3Dot(q, q);
      setMatSkew(-1.0, q, C);
      C[0] = C[4] = C[8] = 1.0 - 0.5*qTq;
      vec3x3OuterAdd(0.5, q, q, C);

      C += 9;
      q += vars_per_node;
    }
  }

  /*
    Compute the derivative of the rotation matrices at each node

    @param vars The full variable vector
    @param varsd The full variable vector
    @param C The rotation matrices at each point
    @param Cd The rotation matrices at each point
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void computeRotationMatDeriv( const TacsScalar vars[],
                                       const TacsScalar varsd[],
                                       TacsScalar C[],
                                       TacsScalar Cd[] ){
    const TacsScalar *q = &vars[offset];
    const TacsScalar *qd = &varsd[offset];
    for ( int i = 0; i < num_nodes; i++ ){
      // Compute C
      TacsScalar qTq = vec3Dot(q, q);
      setMatSkew(-1.0, q, C);
      C[0] = C[4] = C[8] = 1.0 - 0.5*qTq;
      vec3x3OuterAdd(0.5, q, q, C);

      // Compute Cd
      TacsScalar qTqd = vec3Dot(q, qd);
      setMatSkew(-1.0, qd, Cd);
      Cd[0] = Cd[4] = Cd[8] = -qTqd;
      vec3x3OuterAdd(0.5, qd, q, Cd);
      vec3x3OuterAdd(0.5, q, qd, Cd);

      C += 9;
      Cd += 9;

      q += vars_per_node;
      qd += vars_per_node;
    }
  }

  /*
    Add the residual rotation matrix to the output

    This code adds the contribution to the residual via the derivative

    d(tr(dC^{T}C(q)))/dq_{i}
    = d(tr(dC^{T}*(I - q^{x} + 0.5*(qq^{T} - q^{T}q*1))))/dq_{i}

    @param vars The full variable vector
    @param dC The derivative w.r.t. the rotation matrix
    @param res The residual array
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void addRotationMatResidual( const TacsScalar vars[],
                                      const TacsScalar dC[],
                                      TacsScalar res[] ){
    const TacsScalar *q = &vars[offset];
    TacsScalar *r = &res[offset];

    for ( int i = 0; i < num_nodes; i++ ){
      TacsScalar dCtr = (dC[0] + dC[4] + dC[8]);
      r[0] -= dC[7] - dC[5] + dCtr*q[0];
      r[1] -= dC[2] - dC[6] + dCtr*q[1];
      r[2] -= dC[3] - dC[1] + dCtr*q[2];

      TacsScalar e1[3], e2[3];
      mat3x3Mult(dC, q, e1);
      mat3x3MultTrans(dC, q, e2);

      r[0] += 0.5*(e1[0] + e2[0]);
      r[1] += 0.5*(e1[1] + e2[1]);
      r[2] += 0.5*(e1[2] + e2[2]);

      r += vars_per_node;
      q += vars_per_node;
      dC += 9;
    }
  }

  /*
    Add the
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void addRotationMatJacobian( const TacsScalar vars[],
                                      const TacsScalar d2C[],
                                      TacsScalar mat[] ){
    const int size = vars_per_node*num_nodes;
    const int csize = 9*num_nodes;

    for ( int i = 0; i < num_nodes; i++ ){
      TacsScalar *m = &mat[offset*(size + 1)];

      for ( int j = 0; j < num_nodes; j++ ){
        // r[0] += -(dC[7] - dC[5]);
        // r[1] += -(dC[2] - dC[6]);
        // r[2] += -(dC[3] - dC[1]);
        // r += vars_per_node;
        // dC += 9;

        // Add the non-zero entries
        // m[0] += d2C[];



        m += vars_per_node;

        d2C += 9;
      }

      mat += vars_per_node*size;
      d2C += 8*csize;
    }
  }

  /**
    Compute the director and rates at all nodes.

    d = Q(q)*t = (1 + 0.5*q^{x})*q^{x}t
    ddot = qdot^{x}*t + 0.5*q^{x}*qdot^{x}*t + 0.5*qdot^{x}*q^{x}*t

    @param vars The full variable vector
    @param dvars The first time derivative of the variables
    @param ddvars The second derivatives of the variables
    @param t The reference directions
    @param d The director values
    @param ddot The first time derivative of the director
    @param dddot The second time derivative of the director
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void computeDirectorRates( const TacsScalar vars[],
                                    const TacsScalar dvars[],
                                    const TacsScalar t[],
                                    TacsScalar d[],
                                    TacsScalar ddot[] ){
    const TacsScalar *q = &vars[offset];
    const TacsScalar *qdot = &dvars[offset];
    for ( int i = 0; i < num_nodes; i++ ){
      TacsScalar qxt[3], qxtdot[3];

      // Compute d = q^{x}*t + 0.5*q^{x}*q^{x}*t
      crossProduct(q, t, qxt);
      d[0] = qxt[0];
      d[1] = qxt[1];
      d[2] = qxt[2];
      crossProductAdd(0.5, q, qxt, d);

      // Compute ddot =
      // qdot^{x}*t + 0.5*qdot^{x}*q^{x}*t + 0.5*q^{x}*qdot^{x}*t
      crossProduct(qdot, t, qxtdot);
      ddot[0] = qxtdot[0];
      ddot[1] = qxtdot[1];
      ddot[2] = qxtdot[2];
      crossProductAdd(0.5, qdot, qxt, ddot);
      crossProductAdd(0.5, q, qxtdot, ddot);

      t += 3;
      d += 3;
      ddot += 3;

      q += vars_per_node;
      qdot += vars_per_node;
    }
  }

  /**
    Compute the director and rates at all nodes.

    d = Q(q)*t = (C(q)^{T} - I)*t
    ddot = d/dt(Q(q))*t
    dddot = d^2/dt^2(Q(q))*t

    @param vars The full variable vector
    @param dvars The first time derivative of the variables
    @param ddvars The second derivatives of the variables
    @param t The reference directions
    @param d The director values
    @param ddot The first time derivative of the director
    @param dddot The second time derivative of the director
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void computeDirectorRates( const TacsScalar vars[],
                                    const TacsScalar dvars[],
                                    const TacsScalar ddvars[],
                                    const TacsScalar t[],
                                    TacsScalar d[],
                                    TacsScalar ddot[],
                                    TacsScalar dddot[] ){
    const TacsScalar *q = &vars[offset];
    const TacsScalar *qdot = &dvars[offset];
    const TacsScalar *qddot = &ddvars[offset];
    for ( int i = 0; i < num_nodes; i++ ){
      TacsScalar qxt[3], qxtdot[3], qxtddot[3];

      // Compute d = q^{x}*t + 0.5*q^{x}*q^{x}*t
      crossProduct(q, t, qxt);
      d[0] = qxt[0];
      d[1] = qxt[1];
      d[2] = qxt[2];
      crossProductAdd(0.5, q, qxt, d);

      // Compute ddot =
      // qdot^{x}*t + 0.5*qdot^{x}*q^{x}*t + 0.5*q^{x}*qdot^{x}*t
      crossProduct(qdot, t, qxtdot);
      ddot[0] = qxtdot[0];
      ddot[1] = qxtdot[1];
      ddot[2] = qxtdot[2];
      crossProductAdd(0.5, qdot, qxt, ddot);
      crossProductAdd(0.5, q, qxtdot, ddot);

      // Compute ddot =
      // qddot^{x}*t + 0.5*qddot^{x}*q^{x}*t +
      // qdot^{x}*qdot^{x}*t + 0.5*q^{x}*qddot^{x}*t
      crossProduct(qddot, t, qxtddot);
      dddot[0] = qxtddot[0];
      dddot[1] = qxtddot[1];
      dddot[2] = qxtddot[2];
      crossProductAdd(0.5, qddot, qxt, dddot);
      crossProductAdd(1.0, qdot, qxtdot, dddot);
      crossProductAdd(0.5, q, qxtddot, dddot);

      t += 3;
      d += 3;
      ddot += 3;
      dddot += 3;

      q += vars_per_node;
      qdot += vars_per_node;
      qddot += vars_per_node;
    }
  }

  /**
    Compute the director and rates at all nodes and the derivative.

    d = Q(q)*t = (C(q)^{T} - I)*t
    ddot = d/dt(Q(q))*t
    dddot = d^2/dt^2(Q(q))*t

    @param vars The full variable vector
    @param dvars The first time derivative of the variables
    @param ddvars The second derivatives of the variables
    @param varsd The full variable vector derivative
    @param t The reference directions
    @param C The rotation matrices at each point
    @param d The director values
    @param ddot The first time derivative of the director
    @param dddot The second time derivative of the director
    @param Cd The derivative of the rotation matrices at each point
    @param dd The derivator of the director values
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void computeDirectorRatesDeriv( const TacsScalar vars[],
                                         const TacsScalar dvars[],
                                         const TacsScalar ddvars[],
                                         const TacsScalar varsd[],
                                         const TacsScalar t[],
                                         TacsScalar d[],
                                         TacsScalar ddot[],
                                         TacsScalar dddot[],
                                         TacsScalar dd[] ){
    const TacsScalar *q = &vars[offset];
    const TacsScalar *qdot = &dvars[offset];
    const TacsScalar *qddot = &ddvars[offset];
    const TacsScalar *qd = &varsd[offset];
    for ( int i = 0; i < num_nodes; i++ ){
      TacsScalar qxt[3], qxtdot[3], qxtddot[3];

      // Compute d = q^{x}*t + 0.5*q^{x}*q^{x}*t
      crossProduct(q, t, qxt);
      d[0] = qxt[0];
      d[1] = qxt[1];
      d[2] = qxt[2];
      crossProductAdd(0.5, q, qxt, d);

      // Compute ddot =
      // qdot^{x}*t + 0.5*qdot^{x}*q^{x}*t + 0.5*q^{x}*qdot^{x}*t
      crossProduct(qdot, t, qxtdot);
      ddot[0] = qxtdot[0];
      ddot[1] = qxtdot[1];
      ddot[2] = qxtdot[2];
      crossProductAdd(0.5, qdot, qxt, ddot);
      crossProductAdd(0.5, q, qxtdot, ddot);

      // Compute ddot =
      // qddot^{x}*t + 0.5*qddot^{x}*q^{x}*t +
      // qdot^{x}*qdot^{x}*t + 0.5*q^{x}*qddot^{x}*t
      crossProduct(qddot, t, qxtddot);
      dddot[0] = qxtddot[0];
      dddot[1] = qxtddot[1];
      dddot[2] = qxtddot[2];
      crossProductAdd(0.5, qddot, qxt, dddot);
      crossProductAdd(1.0, qdot, qxtdot, dddot);
      crossProductAdd(0.5, q, qxtddot, dddot);

      // Cd = - qd^{x}
      crossProduct(qd, t, dd);

      t += 3;
      d += 3;
      dd += 3;
      ddot += 3;
      dddot += 3;

      q += vars_per_node;
      qdot += vars_per_node;
      qddot += vars_per_node;
      qd += vars_per_node;
    }
  }

  /**
    Given the derivatives of the kinetic energy expression with
    respect to time, add the contributions to the derivative of the

    Given the partial derivatives of the Lagrangian with respect to the
    director and the time derivative of the vector, compute

    dTdot = d/dt(dT/d(dot{d}))
    dT = dT/d(dot{d})
    dd = -dL/dd

    In general, the residual contribution is:

    res +=
    dTdot*d(dot{d})/d(dot{q}) +
    dT*d/dt(dot{d})/d(dot{q}) +
    dd*d(d)/d(q)

    For the quadratic director these expressions are divided into the
    kinetic energy terms given as:

    d(dot{d})/d(dot{q}) = -t^{x} - 0.5*q^{x}*t^{x} - 0.5*(q^{x}*t)^{x}

    [dTdot*d(dot{d})/d(dot{q})]^{T} =
    t^{x}*dTdot - 0.5*t^{x}*q^{x}*dTdot + 0.5*(q^{x}*t)^{x}*dTdot

    d/dt(d(dot{d})/d(dot{q})) = - 0.5*qdot^{x}*t^{x} - 0.5*(qdot^{x}*t)^{x}

    [dT*d/dt(dot{d})/d(dot{q})]^{T} =
    - 0.5*t^{x}*qdot^{x}*dT + 0.5*(qdot^{x}*t)^{x}*dT

    And the potential energy term

    d(d)/d(q) = -t^{x} - 0.5*(q^{x}t^{x} - (q^{x}t)^{x})
    dd*d(d)/d(q) = -dd*t^{x} - 0.5*dd*q^{x}*t^{x} - 0.5*dd*(q^{x}*t)^{x}
    [dd*d(d)/d(q)]^{T} = t^{x}*dd - 0.5*t^{x}*q^{x}*dd + 0.5*(q^{x}*t)^{x}*dd

    @param vars The full variable vector
    @param dvars The first time derivative of the variables
    @param ddvars The second derivatives of the variables
    @param t The normal direction
    @param dTdot Time deriv. of the deriv. of the kinetic energy w.r.t. d
    @param dT The derivative of the kinetic energy w.r.t. director
    @param dd The contribution from the derivative of the director
    @param res The output residual
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void addDirectorResidual( const TacsScalar vars[],
                                   const TacsScalar dvars[],
                                   const TacsScalar ddvars[],
                                   const TacsScalar t[],
                                   const TacsScalar dTdot[],
                                   const TacsScalar dT[],
                                   const TacsScalar dd[],
                                   TacsScalar res[] ){
    TacsScalar *r = &res[offset];
    const TacsScalar *q = &vars[offset];
    const TacsScalar *qdot = &dvars[offset];

    for ( int i = 0; i < num_nodes; i++ ){
      TacsScalar v[3], qxt[3], qdotxt[3];

      // Compute the contribution to the residual:
      crossProductAdd(1.0, t, dd, r);
      crossProduct(q, t, qxt);
      crossProductAdd(0.5, qxt, dd, r);
      crossProduct(q, dd, v);
      crossProductAdd(-0.5, t, v, r);

      // Compute the contributions to the residual
      crossProductAdd(1.0, t, dTdot, r);
      crossProductAdd(0.5, qxt, dTdot, r);
      crossProduct(q, dTdot, v);
      crossProductAdd(-0.5, t, v, r);

      crossProduct(qdot, t, qdotxt);
      crossProductAdd(0.5, qdotxt, dT, r);
      crossProduct(qdot, dT, v);
      crossProductAdd(-0.5, t, v, r);

      r += vars_per_node;
      q += vars_per_node;
      qdot += vars_per_node;

      dd += 3;
      dTdot += 3;
      dT += 3;
      t += 3;
    }
  }

  /*
    Add terms from the Jacobian
  */
  template <int vars_per_node, int offset, int num_nodes>
  static void addDirectorJacobian( const TacsScalar vars[],
                                   const TacsScalar dvars[],
                                   const TacsScalar ddvars[],
                                   const TacsScalar t[],
                                   const TacsScalar d2d[],
                                   const TacsScalar d2du[],
                                   TacsScalar mat[] ){
  }

};




















/*
  The director class.

  Given a reference vector, t, from the element geometry, the director computes
  the exact or approximate rate of change of the displacement t.
*/

/**
  Compute the director at a point.

  d = Q(q)*t = (C(q)^{T} - I)*t

  @param q The input rotation parametrization
  @param t The reference direction
  @param d The director values
*/
// static void computeDirector( const TacsScalar q[],
//                              const TacsScalar t[],
//                              TacsScalar d[] ){
//   // Compute Q = C^{T} - I
//   TacsScalar Q[9];
//   Q[0] =-2.0*(q[2]*q[2] + q[3]*q[3]);
//   Q[1] = 2.0*(q[2]*q[1] - q[3]*q[0]);
//   Q[2] = 2.0*(q[3]*q[1] + q[2]*q[0]);

//   Q[3] = 2.0*(q[1]*q[2] + q[3]*q[0]);
//   Q[4] =-2.0*(q[1]*q[1] + q[3]*q[3]);
//   Q[5] = 2.0*(q[3]*q[2] - q[1]*q[0]);

//   Q[6] = 2.0*(q[1]*q[3] - q[2]*q[0]);
//   Q[7] = 2.0*(q[2]*q[3] + q[1]*q[0]);
//   Q[8] =-2.0*(q[1]*q[1] + q[2]*q[2]);

//   // Compute d = Q*t
//   d[0] = Q[0]*t[0] + Q[1]*t[1] + Q[2]*t[2];
//   d[1] = Q[3]*t[0] + Q[4]*t[1] + Q[5]*t[2];
//   d[2] = Q[6]*t[0] + Q[7]*t[1] + Q[8]*t[2];
// }

template <int vars_per_node, int offset, int num_nodes, class director>
int TacsTestDirector( double dh=1e-7,
                      int test_print_level=2,
                      double test_fail_atol=1e-5,
                      double test_fail_rtol=1e-5 ){
  const int size = vars_per_node*num_nodes;
  const int dsize = 3*num_nodes;
  const int csize = 9*num_nodes;

  // Generate random arrays for the state variables and their time derivatives
  TacsScalar vars[size], dvars[size], ddvars[size];
  TacsGenerateRandomArray(vars, size);
  TacsGenerateRandomArray(dvars, size);
  TacsGenerateRandomArray(ddvars, size);

  // Compute/normalize the normals
  TacsScalar t[dsize];
  TacsGenerateRandomArray(t, dsize);
  for ( int i = 0; i < num_nodes; i++ ){
    TacsScalar tnrm = sqrt(vec3Dot(&t[3*i], &t[3*i]));
    vec3Scale(1.0/tnrm, &t[3*i]);
  }

  // Random perturbation for the variables
  TacsScalar varsd[size];
  TacsGenerateRandomArray(varsd, size);

  // Create random arrays for testing the residual and Jacobian
  TacsScalar dC[csize], d2C[csize*csize];
  TacsGenerateRandomArray(dC, csize);
  TacsGenerateRandomArray(d2C, csize*csize);
  for ( int i = 0; i < csize; i++ ){
    for ( int j = 0; j < i; j++ ){
      d2C[j + i*csize] = d2C[i + j*csize];
    }
  }

  // Compute the rotation matrices
  TacsScalar C[csize];
  director::template computeRotationMat<vars_per_node, offset, num_nodes>(vars, C);

  // Compute the residual
  TacsScalar res[size];
  memset(res, 0, size*sizeof(TacsScalar));
  director::template addRotationMatResidual<vars_per_node, offset, num_nodes>(vars, dC, res);

  // Compute the Jacobian
  TacsScalar mat[size*size];
  memset(mat, 0, size*size*sizeof(TacsScalar));
  director::template addRotationMatJacobian<vars_per_node, offset, num_nodes>(vars, d2C, mat);

  // Verify the implementation of the residual
  TacsScalar fd[size], C0 = 0.0;
  for ( int i = 0; i < csize; i++ ){
    C0 += dC[i]*C[i];
  }

  for ( int k = 0; k < size; k++ ){
    TacsScalar varst[size];
    memcpy(varst, vars, size*sizeof(TacsScalar));

#ifdef TACS_USE_COMPLEX
    varst[k] = vars[k] + TacsScalar(0.0, dh);
#else
    varst[k] = vars[k] + dh;
#endif // TACS_USE_COMPLEX

    TacsScalar Ct[csize];
    director::template computeRotationMat<vars_per_node, offset, num_nodes>(varst, Ct);

    TacsScalar C1 = 0.0;
    for ( int i = 0; i < csize; i++ ){
      C1 += dC[i]*Ct[i];
    }

#ifdef TACS_USE_COMPLEX
    fd[k] = TacsImagPart(C1)/dh;
#else
    fd[k] = (C1 - C0)/dh;
#endif // TACS_USE_COMPLEX
  }

  // Variables to store the max error and indices
  int max_err_index, max_rel_index;
  double max_err, max_rel;

  // Keep track of the failure flag
  int fail = 0;

  // Compute the error
  max_err = TacsGetMaxError(res, fd, size, &max_err_index);
  max_rel = TacsGetMaxRelError(res, fd, size, &max_rel_index);

  if (test_print_level > 0){
    fprintf(stderr, "Testing the director residual implementation\n");
    fprintf(stderr, "Max Err: %10.4e in component %d.\n",
            max_err, max_err_index);
    fprintf(stderr, "Max REr: %10.4e in component %d.\n",
            max_rel, max_rel_index);
  }
  // Print the error if required
  if (test_print_level > 1){
    TacsPrintErrorComponents(stderr, "res", res, fd, size);
  }
  if (test_print_level){ fprintf(stderr, "\n"); }

  fail = (max_err > test_fail_atol || max_rel > test_fail_rtol);

  // Compute the derivative of the rotation matrix
  TacsScalar Cd[csize];
  director::template computeRotationMatDeriv<vars_per_node, offset, num_nodes>(vars, varsd, C, Cd);

  TacsScalar q[size];
  for ( int k = 0; k < size; k++ ){
#ifdef TACS_USE_COMPLEX
    q[k] = vars[k] + varsd[k]*TacsScalar(0.0, dh);
#else
    q[k] = vars[k] + varsd[k]*dh;
#endif // TACS_USE_COMPLEX
  }

  TacsScalar Ctemp[csize];
  director::template computeRotationMat<vars_per_node, offset, num_nodes>(q, Ctemp);

  TacsScalar fdC[csize];
  for ( int k = 0; k < csize; k++ ){
#ifdef TACS_USE_COMPLEX
    fdC[k] = TacsImagPart(Ctemp[k])/dh;
#else
    fdC[k] = (Ctemp[k] - C[k])/dh;
#endif // TACS_USE_COMPLEX
  }

  // Compute the error
  max_err = TacsGetMaxError(Cd, fdC, csize, &max_err_index);
  max_rel = TacsGetMaxRelError(Cd, fdC, csize, &max_rel_index);

  if (test_print_level > 0){
    fprintf(stderr, "Testing the derivative of the rotation matrix\n");
    fprintf(stderr, "Max Err: %10.4e in component %d.\n",
            max_err, max_err_index);
    fprintf(stderr, "Max REr: %10.4e in component %d.\n",
            max_rel, max_rel_index);
  }
  // Print the error if required
  if (test_print_level > 1){
    TacsPrintErrorComponents(stderr, "Cd", Cd, fdC, csize);
  }
  if (test_print_level){ fprintf(stderr, "\n"); }

  fail = (max_err > test_fail_atol || max_rel > test_fail_rtol);

  // Compute the derivative of the rotation matrix residual
  TacsScalar fdmat[size*size];
  for ( int k = 0; k < size; k++ ){
    TacsScalar varst[size];
    memcpy(varst, vars, size*sizeof(TacsScalar));

#ifdef TACS_USE_COMPLEX
    varst[k] = vars[k] + TacsScalar(0.0, dh);
#else
    varst[k] = vars[k] + dh;
#endif // TACS_USE_COMPLEX

    TacsScalar Ct[csize];
    director::template computeRotationMat<vars_per_node, offset, num_nodes>(varst, Ct);

    // Add the contributions from the
    TacsScalar dCt[csize];
    for ( int i = 0; i < csize; i++ ){
      dCt[i] = dC[i];

      for ( int j = 0; j < csize; j++ ){
        dCt[i] += d2C[j + i*csize]*(Ct[j] - C[j]);
      }
    }

    TacsScalar rest[size];
    memset(rest, 0, size*sizeof(TacsScalar));
    director::template addRotationMatResidual<vars_per_node, offset, num_nodes>(varst, dCt, rest);

    for ( int j = 0; j < size; j++ ){
#ifdef TACS_USE_COMPLEX
      fdmat[k + size*j] = TacsImagPart(rest[j])/dh;
#else
      fdmat[k + size*j] = (rest[j] - res[j])/dh;
#endif // TACS_USE_COMPLEX
    }
  }

  // Compute the error
  max_err = TacsGetMaxError(mat, fdmat, size*size, &max_err_index);
  max_rel = TacsGetMaxRelError(mat, fdmat, size*size, &max_rel_index);

  if (test_print_level > 0){
    fprintf(stderr, "Testing the derivative of the rotation matrix w.r.t. vars\n");
    fprintf(stderr, "Max Err: %10.4e in component %d.\n",
            max_err, max_err_index);
    fprintf(stderr, "Max REr: %10.4e in component %d.\n",
            max_rel, max_rel_index);
  }
  // Print the error if required
  if (test_print_level > 1){
    TacsPrintErrorComponents(stderr, "mat", mat, fdmat, size*size);
  }
  if (test_print_level){ fprintf(stderr, "\n"); }

  fail = (max_err > test_fail_atol || max_rel > test_fail_rtol);

  // Check for consistency between the director and C
  director::template computeRotationMat<vars_per_node, offset, num_nodes>(vars, C);

  TacsScalar d[dsize], ddot[dsize];
  director::template computeDirectorRates<vars_per_node, offset, num_nodes>(vars, dvars, t, d, ddot);

  TacsScalar dcal[dsize];
  for ( int i = 0; i < num_nodes; i++ ){
    const TacsScalar *C0 = &C[9*i];
    TacsScalar Qt[9];
    Qt[0] = C0[0] - 1.0;
    Qt[1] = C0[1];
    Qt[2] = C0[2];
    Qt[3] = C0[3];
    Qt[4] = C0[4] - 1.0;
    Qt[5] = C0[5];
    Qt[6] = C0[6];
    Qt[7] = C0[7];
    Qt[8] = C0[8] - 1.0;

    mat3x3MultTrans(Qt, &t[3*i], &dcal[3*i]);
  }

  // Compute the error
  max_err = TacsGetMaxError(d, dcal, dsize, &max_err_index);
  max_rel = TacsGetMaxRelError(d, dcal, dsize, &max_rel_index);

  if (test_print_level > 0){
    fprintf(stderr, "Testing the consistency of the director\n");
    fprintf(stderr, "Max Err: %10.4e in component %d.\n",
            max_err, max_err_index);
    fprintf(stderr, "Max REr: %10.4e in component %d.\n",
            max_rel, max_rel_index);
  }
  // Print the error if required
  if (test_print_level > 1){
    TacsPrintErrorComponents(stderr, "d", d, dcal, dsize);
  }
  if (test_print_level){ fprintf(stderr, "\n"); }

  fail = (max_err > test_fail_atol || max_rel > test_fail_rtol);

  // Test the implementation of the director time derivative
  TacsScalar dddot[dsize];
  director::template computeDirectorRates<vars_per_node, offset, num_nodes>(vars, dvars, ddvars,
                                                                            t, d, ddot, dddot);

  TacsScalar fddot[dsize], varst[size];
  for ( int k = 0; k < size; k++ ){
#ifdef TACS_USE_COMPLEX
    varst[k] = vars[k] + dvars[k]*TacsScalar(0.0, dh);
#else
    varst[k] = vars[k] + dh*dvars[k];
#endif // TACS_USE_COMPLEX
  }

  TacsScalar dt[dsize], dtdot[dsize];
  director::template computeDirectorRates<vars_per_node, offset, num_nodes>(varst, dvars, t, dt, dtdot);

  for ( int k = 0; k < dsize; k++ ){
#ifdef TACS_USE_COMPLEX
    fddot[k] = TacsImagPart(dt[k])/dh;
#else
    fddot[k] = (dt[k] - d[k])/dh;
#endif // TACS_USE_COMPLEX
  }

  // Compute the error
  max_err = TacsGetMaxError(ddot, fddot, dsize, &max_err_index);
  max_rel = TacsGetMaxRelError(ddot, fddot, dsize, &max_rel_index);

  if (test_print_level > 0){
    fprintf(stderr, "Testing the time derivative of the director\n");
    fprintf(stderr, "Max Err: %10.4e in component %d.\n",
            max_err, max_err_index);
    fprintf(stderr, "Max REr: %10.4e in component %d.\n",
            max_rel, max_rel_index);
  }
  // Print the error if required
  if (test_print_level > 1){
    TacsPrintErrorComponents(stderr, "ddot", ddot, fddot, dsize);
  }
  if (test_print_level){ fprintf(stderr, "\n"); }

  fail = (max_err > test_fail_atol || max_rel > test_fail_rtol);

  TacsScalar fdddot[dsize], dvarst[size];
  for ( int k = 0; k < size; k++ ){
#ifdef TACS_USE_COMPLEX
    varst[k] = vars[k] + dvars[k]*TacsScalar(0.0, dh) + 0.5*ddvars[k]*TacsScalar(0.0, dh*dh);
    dvarst[k] = dvars[k] + ddvars[k]*TacsScalar(0.0, dh);
#else
    varst[k] = vars[k] + dh*dvars[k] + 0.5*dh*dh*ddvars[k];
    dvarst[k] = dvars[k] + dh*ddvars[k];
#endif // TACS_USE_COMPLEX
  }

  director::template computeDirectorRates<vars_per_node, offset, num_nodes>(varst, dvarst, t, dt, dtdot);

  for ( int k = 0; k < dsize; k++ ){
#ifdef TACS_USE_COMPLEX
    fdddot[k] = TacsImagPart(dtdot[k])/dh;
#else
    fdddot[k] = (dtdot[k] - ddot[k])/dh;
#endif // TACS_USE_COMPLEX
  }

  // Compute the error
  max_err = TacsGetMaxError(dddot, fdddot, dsize, &max_err_index);
  max_rel = TacsGetMaxRelError(dddot, fdddot, dsize, &max_rel_index);

  if (test_print_level > 0){
    fprintf(stderr, "Testing the second time derivative of the director\n");
    fprintf(stderr, "Max Err: %10.4e in component %d.\n",
            max_err, max_err_index);
    fprintf(stderr, "Max REr: %10.4e in component %d.\n",
            max_rel, max_rel_index);
  }
  // Print the error if required
  if (test_print_level > 1){
    TacsPrintErrorComponents(stderr, "dddot", dddot, fdddot, dsize);
  }
  if (test_print_level){ fprintf(stderr, "\n"); }

  fail = (max_err > test_fail_atol || max_rel > test_fail_rtol);
  return fail;
}

template <int dsize>
void TacsTestEvalDirectorEnergy( const TacsScalar Tlin[],
                                 const TacsScalar Tquad[],
                                 const TacsScalar Plin[],
                                 const TacsScalar Pquad[],
                                 const TacsScalar d[],
                                 const TacsScalar ddot[],
                                 TacsScalar *_T,
                                 TacsScalar *_P ){
  TacsScalar T = 0.0;
  TacsScalar P = 0.0;

  for ( int j = 0; j < dsize; j++ ){
    T += Tlin[j]*ddot[j];
    P += Plin[j]*d[j];
    for ( int i = 0; i < dsize; i++ ){
      // T += Tquad[i + j*dsize]*ddot[i]*ddot[j];
      P += Pquad[i + j*dsize]*d[i]*d[j];
    }
  }

  *_T = T;
  *_P = P;
}

template <int dsize>
void TacsTestEvalDirectorEnergyDerivatives( const TacsScalar Tlin[],
                                            const TacsScalar Tquad[],
                                            const TacsScalar Plin[],
                                            const TacsScalar Pquad[],
                                            const TacsScalar d[],
                                            const TacsScalar ddot[],
                                            const TacsScalar dddot[],
                                            TacsScalar dTdot[],
                                            TacsScalar dT[],
                                            TacsScalar dd[] ){
  for ( int j = 0; j < dsize; j++ ){
    dT[j] = Tlin[j];
    dTdot[j] = 0.0;
    dd[j] = Plin[j];
  }

  for ( int j = 0; j < dsize; j++ ){
    for ( int i = 0; i < dsize; i++ ){
      // dT[j] += Tquad[i + j*dsize]*ddot[i];
      // dT[i] += Tquad[i + j*dsize]*ddot[j];

      // dTdot[j] += Tquad[i + j*dsize]*dddot[i];
      // dTdot[i] += Tquad[i + j*dsize]*dddot[j];

      dd[j] += Pquad[i + j*dsize]*d[i];
      dd[i] += Pquad[i + j*dsize]*d[j];
    }
  }
}

template <int vars_per_node, int offset, int num_nodes, class director>
int TacsTestDirectorResidual( double dh=1e-5,
                              int test_print_level=2,
                              double test_fail_atol=1e-5,
                              double test_fail_rtol=1e-5 ){
  const int size = vars_per_node*num_nodes;
  const int dsize = 3*num_nodes;

  // Generate random arrays for the state variables and their time derivatives
  TacsScalar vars[size], dvars[size], ddvars[size];
  TacsGenerateRandomArray(vars, size);
  TacsGenerateRandomArray(dvars, size);
  TacsGenerateRandomArray(ddvars, size);

  // Compute/normalize the normals
  TacsScalar t[dsize];
  TacsGenerateRandomArray(t, dsize);
  for ( int i = 0; i < num_nodes; i++ ){
    TacsScalar tnrm = sqrt(vec3Dot(&t[3*i], &t[3*i]));
    vec3Scale(1.0/tnrm, &t[3*i]);
  }

  // Compute the director rates
  TacsScalar d[dsize], ddot[dsize], dddot[dsize];
  director::template
    computeDirectorRates<vars_per_node, offset, num_nodes>(vars, dvars, ddvars, t, d, ddot, dddot);

  // The kinetic energy is computed as
  TacsScalar Tlin[dsize], Plin[dsize];
  TacsGenerateRandomArray(Tlin, dsize);
  TacsGenerateRandomArray(Plin, dsize);

  TacsScalar Tquad[dsize*dsize], Pquad[dsize*dsize];
  TacsGenerateRandomArray(Tquad, dsize*dsize);
  TacsGenerateRandomArray(Pquad, dsize*dsize);

  // Compute the derivatives of the kinetic and potential energies
  TacsScalar dTdot[dsize], dT[dsize], dd[dsize];
  TacsTestEvalDirectorEnergyDerivatives<dsize>(Tlin, Tquad, Plin, Pquad, d, ddot, dddot,
                                               dTdot, dT, dd);

  // Compute the residual
  TacsScalar res[size];
  memset(res, 0, size*sizeof(TacsScalar));
  director::template addDirectorResidual<vars_per_node, offset, num_nodes>(vars, dvars, ddvars, t,
                                                                           dTdot, dT, dd, res);

  // Compute the values of the variables at (t + dt)
  TacsScalar q[size], qdot[size];
  for ( int i = 0; i < size; i++ ){
    q[i] = vars[i] + dh*dvars[i] + 0.5*dh*dh*ddvars[i];
    qdot[i] = dvars[i] + dh*ddvars[i];
  }

  // Evaluate the derivative w.r.t. dot{q}
  TacsScalar res1[size];
  for ( int i = 0; i < size; i++ ){
    // Evaluate the finite-difference for component i
    TacsScalar dqtmp = qdot[i];
#ifdef TACS_USE_COMPLEX
    TacsScalar T1, P1;
    qdot[i] = dqtmp + TacsScalar(0.0, dh);
    director::template computeDirectorRates<vars_per_node, offset, num_nodes>(q, qdot, t, d, ddot);
    TacsTestEvalDirectorEnergy<dsize>(Tlin, Tquad, Plin, Pquad, d, ddot, &T1, &P1);
    res1[i] = TacsImagPart((T1 - P1))/dh;
#else
    TacsScalar T1, P1, T2, P2;
    qdot[i] = dqtmp + dh;
    director::template computeDirectorRates<vars_per_node, offset, num_nodes>(q, qdot, t, d, ddot);
    TacsTestEvalDirectorEnergy<dsize>(Tlin, Tquad, Plin, Pquad, d, ddot, &T1, &P1);

    qdot[i] = dqtmp - dh;
    director::template computeDirectorRates<vars_per_node, offset, num_nodes>(q, qdot, t, d, ddot);
    TacsTestEvalDirectorEnergy<dsize>(Tlin, Tquad, Plin, Pquad, d, ddot, &T2, &P2);
    res1[i] = 0.5*((T1 - P1) - (T2 - P2))/dh;
#endif
    qdot[i] = dqtmp;
  }

  // Compute the values of the variables at (t - dt)
  for ( int i = 0; i < size; i++ ){
    q[i] = vars[i] - dh*dvars[i] - 0.5*dh*dh*ddvars[i];
    qdot[i] = dvars[i] - dh*ddvars[i];
  }

  // Evaluate the derivative w.r.t. dot{q}
  TacsScalar res2[size];
  for ( int i = 0; i < size; i++ ){
    // Evaluate the finite-difference for component i
    TacsScalar dqtmp = qdot[i];
#ifdef TACS_USE_COMPLEX
    TacsScalar T1, P1;
    qdot[i] = dqtmp + TacsScalar(0.0, dh);
    director::template computeDirectorRates<vars_per_node, offset, num_nodes>(q, qdot, t, d, ddot);
    TacsTestEvalDirectorEnergy<dsize>(Tlin, Tquad, Plin, Pquad, d, ddot, &T1, &P1);
    res2[i] = TacsImagPart((T1 - P1))/dh;
#else
    TacsScalar T1, P1, T2, P2;
    qdot[i] = dqtmp + dh;
    director::template computeDirectorRates<vars_per_node, offset, num_nodes>(q, qdot, t, d, ddot);
    TacsTestEvalDirectorEnergy<dsize>(Tlin, Tquad, Plin, Pquad, d, ddot, &T1, &P1);

    qdot[i] = dqtmp - dh;
    director::template computeDirectorRates<vars_per_node, offset, num_nodes>(q, qdot, t, d, ddot);
    TacsTestEvalDirectorEnergy<dsize>(Tlin, Tquad, Plin, Pquad, d, ddot, &T2, &P2);
    res2[i] = 0.5*((T1 - P1) - (T2 - P2))/dh;
#endif
    qdot[i] = dqtmp;
  }

  // Evaluate the finite-difference for the first term in Largrange's
  // equations of motion
  TacsScalar fd[size];
  for ( int i = 0; i < size; i++ ){
    fd[i] = 0.5*(res1[i] - res2[i])/dh;
  }

  // Reset the values of q and dq at time t
  for ( int i = 0; i < size; i++ ){
    q[i] = vars[i];
    qdot[i] = dvars[i];
  }

  // Compute the contribution from dL/dq^{T}
  for ( int i = 0; i < size; i++ ){
    // Evaluate the finite-difference for component i
    TacsScalar qtmp = q[i];

#ifdef TACS_USE_COMPLEX
    TacsScalar T1, P1;
    q[i] = qtmp + TacsScalar(0.0, dh);
    director::template computeDirectorRates<vars_per_node, offset, num_nodes>(q, qdot, t, d, ddot);
    TacsTestEvalDirectorEnergy<dsize>(Tlin, Tquad, Plin, Pquad, d, ddot, &T1, &P1);
    res1[i] = TacsImagPart((T1 - P1))/dh;
#else
    TacsScalar T1, P1, T2, P2;
    q[i] = qtmp + dh;
    director::template computeDirectorRates<vars_per_node, offset, num_nodes>(q, qdot, t, d, ddot);
    TacsTestEvalDirectorEnergy<dsize>(Tlin, Tquad, Plin, Pquad, d, ddot, &T1, &P1);

    q[i] = qtmp - dh;
    director::template computeDirectorRates<vars_per_node, offset, num_nodes>(q, qdot, t, d, ddot);
    TacsTestEvalDirectorEnergy<dsize>(Tlin, Tquad, Plin, Pquad, d, ddot, &T2, &P2);

    // Compute and store the approximation
    res1[i] = 0.5*((T1 - P1) - (T2 - P2))/dh;
#endif
    q[i] = qtmp;
  }

  // Add the result to the finite-difference result
  for ( int i = 0; i < size; i++ ){
    fd[i] -= res1[i];
  }

  // Variables to store the max error and indices
  int max_err_index, max_rel_index;
  double max_err, max_rel;

  // Keep track of the failure flag
  int fail = 0;

  // Compute the error
  max_err = TacsGetMaxError(res, fd, size, &max_err_index);
  max_rel = TacsGetMaxRelError(res, fd, size, &max_rel_index);

  if (test_print_level > 0){
    fprintf(stderr, "Testing the residual implementation\n");
    fprintf(stderr, "Max Err: %10.4e in component %d.\n",
            max_err, max_err_index);
    fprintf(stderr, "Max REr: %10.4e in component %d.\n",
            max_rel, max_rel_index);
  }
  // Print the error if required
  if (test_print_level > 1){
    TacsPrintErrorComponents(stderr, "res", res, fd, size);
  }
  if (test_print_level){ fprintf(stderr, "\n"); }

  fail = (max_err > test_fail_atol || max_rel > test_fail_rtol);

  return fail;
}

#endif // TACS_DIRECTOR_H
