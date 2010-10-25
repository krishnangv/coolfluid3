// Copyright (C) 2010 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#ifndef CF_Actions_ProtoTransforms_hpp
#define CF_Actions_ProtoTransforms_hpp

#include <boost/mpl/max.hpp>
#include <boost/mpl/void.hpp>

#include "Actions/Proto/ProtoVariables.hpp"

#include "Math/MatrixTypes.hpp"
#include <boost/variant/variant.hpp>

namespace boost {
template<class T >
struct remove_reference;
}

namespace CF {
namespace Actions {
namespace Proto {


template<typename T>
struct VarArity
{
  typedef typename T::index_type type;
};

/// Gets the arity (max index) for the numbered variables
struct ExprVarArity
  : boost::proto::or_<
        boost::proto::when< boost::proto::terminal< Var<boost::proto::_, boost::proto::_> >,
          boost::mpl::next< VarArity<boost::proto::_value> >()
        >
      , boost::proto::when< boost::proto::terminal<boost::proto::_>,
          boost::mpl::int_<0>()
        >
      , boost::proto::when<
            boost::proto::nary_expr<boost::proto::_, boost::proto::vararg<boost::proto::_> >
          , boost::proto::fold<boost::proto::_, boost::mpl::int_<0>(), boost::mpl::max<ExprVarArity, boost::proto::_state>()>
        >
    >
{};

/// Transform that extracts the type of variable I
template<Uint I>
struct DefineType
  : boost::proto::or_<
      boost::proto::when< boost::proto::terminal< Var<boost::mpl::int_<I>, boost::proto::_> >,
          boost::proto::_value
      >
    , boost::proto::when< boost::proto::terminal< boost::proto::_ >,
          boost::mpl::void_()
      >
    , boost::proto::when< boost::proto::nary_expr<boost::proto::_, boost::proto::vararg<boost::proto::_> >
            , boost::proto::fold<boost::proto::_, boost::mpl::void_(), boost::mpl::if_<boost::mpl::is_void_<boost::proto::_state>, DefineType<I>, boost::proto::_state>() >
      >
  >
{};

/// Ease application of DefineType as an MPL lambda expression, and strip the Var encapsulation
template<typename I, typename Expr>
struct DefineTypeOp
{
  typedef typename boost::result_of<DefineType<I::value>(Expr)>::type var_type;
  typedef typename boost::mpl::if_<boost::mpl::is_void_<var_type>, boost::mpl::void_, typename var_type::type>::type type;
};

/// Copy the terminal values to a fusion list
template<typename VarsT>
struct CopyNumberedVars
  : boost::proto::callable_context< CopyNumberedVars<VarsT>, boost::proto::null_context >
{
  typedef void result_type;
  
  CopyNumberedVars(VarsT& vars) : m_vars(vars) {}

  template<typename I, typename T>
  void operator()(boost::proto::tag::terminal, Var<I, T> val)
  {
    boost::fusion::at<I>(m_vars) = val;
  }
  
private:
  VarsT& m_vars;  
};

/// Extract the real value type of a given type, which might be an Eigen expression
template<typename T>
struct ValueType
{
  typedef typename Eigen::MatrixBase<T>::PlainObject type;
  
  static void set_zero(type& val)
  {
    val.setZero();
  }
};

/// Specialize for Eigen matrices
template<int I, int J>
struct ValueType< Eigen::Matrix<Real, I, J> >
{
  typedef Eigen::Matrix<Real, I, J> type;
  
  static void set_zero(type& val)
  {
    val.setZero();
  }
};

/// Specialise for reals
template<>
struct ValueType<Real>
{
  typedef Real type;
  
  static void set_zero(type& val)
  {
    val = 0.;
  }
};

/// Internal
template<typename ExprT, typename MatrixT>
struct Transform1x1MatrixToScalarHelper
{
  typedef ExprT type;
};

/// Internal
template<typename ExprT>
struct Transform1x1MatrixToScalarHelper< ExprT, Eigen::Matrix<Real, 1, 1> >
{
  typedef Real type;
};

/// Turn 1x1 matrices to scalar
template<typename T>
struct Transform1x1MatrixToScalar
{
  typedef typename Transform1x1MatrixToScalarHelper< T, typename Eigen::MatrixBase<T>::PlainObject >::type type;
};

} // namespace Proto
} // namespace Actions
} // namespace CF

#endif // CF_Actions_ProtoTransforms_hpp