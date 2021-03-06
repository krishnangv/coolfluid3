// Copyright (C) 2010-2011 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

#include "common/Foreach.hpp"
#include "common/Log.hpp"
#include "common/OptionList.hpp"
#include "common/Signal.hpp"
#include "common/Builder.hpp"

#include "NavierStokesPhysics.hpp"

namespace cf3 {
namespace UFEM {

using namespace common;

common::ComponentBuilder < NavierStokesPhysics, physics::PhysModel, LibUFEM > NavierStokesPhysics_Builder;

NavierStokesPhysics::NavierStokesPhysics(const std::string& name): DynamicModel(name), m_recursing(false)
{
  options().add<Real>("reference_velocity")
    .description("Reference velocity for the calculation of the stabilization coefficients")
    .pretty_name("Reference velocity")
    .mark_basic();

  options().add("density", 1.2)
    .description("Mass density (kg / m^3)")
    .pretty_name("Density")
    .attach_trigger(boost::bind(&NavierStokesPhysics::trigger_rho, this))
    .link_to(&m_rho)
    .mark_basic();

  options().add("dynamic_viscosity", 1.7894e-5)
    .description("Dynamic Viscosity (kg / m s)")
    .pretty_name("Dynamic Viscosity")
    .attach_trigger(boost::bind(&NavierStokesPhysics::trigger_mu, this))
    .link_to(&m_mu)
    .mark_basic();

  options().add("kinematic_viscosity", 1.7894e-5/1.2)
    .description("Kinematic Viscosity (m^2/s)")
    .pretty_name("Kinematic Viscosity")
    .attach_trigger(boost::bind(&NavierStokesPhysics::trigger_nu, this));
}

void NavierStokesPhysics::trigger_rho()
{
  m_recursing = true;
  options().set("kinematic_viscosity", m_mu / m_rho);
  m_recursing = false;
}

void NavierStokesPhysics::trigger_mu()
{
  trigger_rho();
}

void NavierStokesPhysics::trigger_nu()
{
  if(m_recursing)
    return;

  trigger_mu();
  throw common::BadValue(FromHere(), "Setting a value for the kinematic_viscosity at " + uri().path() + " is not allowed. Please set rho and mu to obtain the correct value");
}



} // UFEM
} // cf3
