/*
 * Copyright 2017, Andrea Del Prete, LAAS-CNRS
 *
 * This file is part of sot-torque-control.
 * sot-torque-control is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 * sot-torque-control is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.  You should
 * have received a copy of the GNU Lesser General Public License along
 * with sot-torque-control.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sot/torque_control/inverse-dynamics-balance-controller.hh>
#include <sot/core/debug.hh>
#include <dynamic-graph/factory.h>

#include <sot/torque_control/commands-helper.hh>
#include <sot/torque_control/utils/stop-watch.hh>

#include <boost/test/unit_test.hpp>

namespace dynamicgraph
{
  namespace sot
  {
    namespace torque_control
    {
      namespace dg = ::dynamicgraph;
      using namespace dg;
      using namespace dg::command;
      using namespace std;
      using namespace pininvdyn;
      using namespace pininvdyn::trajectories;
      using namespace pininvdyn::math;
      using namespace pininvdyn::contacts;
      using namespace pininvdyn::tasks;
      using namespace pininvdyn::solvers;

#define REQUIRE_FINITE(A) assert(is_finite(A))

//Size to be aligned                "-------------------------------------------------------"
#define PROFILE_TAU_DES_COMPUTATION "InverseDynamicsBalanceController: desired tau"
#define PROFILE_HQP_SOLUTION        "InverseDynamicsBalanceController: HQP"
#define PROFILE_PREPARE_INV_DYN     "InverseDynamicsBalanceController: prepare inv-dyn"
#define PROFILE_READ_INPUT_SIGNALS  "InverseDynamicsBalanceController: read input signals"

#define INPUT_SIGNALS         m_com_ref_posSIN \
                           << m_com_ref_velSIN \
                           << m_com_ref_accSIN \
                           << m_posture_ref_posSIN \
                           << m_posture_ref_velSIN \
                           << m_posture_ref_accSIN \
                           << m_base_orientation_ref_posSIN \
                           << m_base_orientation_ref_velSIN \
                           << m_base_orientation_ref_accSIN \
                           << m_kp_base_orientationSIN \
                           << m_kd_base_orientationSIN \
                           << m_kp_constraintsSIN \
                           << m_kd_constraintsSIN \
                           << m_kp_comSIN \
                           << m_kd_comSIN \
                           << m_kp_postureSIN \
                           << m_kd_postureSIN \
                           << m_kp_posSIN \
                           << m_kd_posSIN \
                           << m_w_comSIN \
                           << m_w_postureSIN \
                           << m_w_base_orientationSIN \
                           << m_w_torquesSIN \
                           << m_w_forcesSIN \
                           << m_weight_contact_forcesSIN \
                           << m_muSIN \
                           << m_contact_pointsSIN \
                           << m_contact_normalSIN \
                           << m_f_minSIN \
                           << m_rotor_inertiasSIN \
                           << m_gear_ratiosSIN \
                           << m_tau_maxSIN \
                           << m_q_minSIN \
                           << m_q_maxSIN \
                           << m_dq_maxSIN \
                           << m_ddq_maxSIN \
                           << m_dt_joint_pos_limitsSIN \
                           << m_tau_estimatedSIN \
                           << m_qSIN \
                           << m_vSIN \
                           << m_wrench_baseSIN \
                           << m_wrench_left_footSIN  \
                           << m_wrench_right_footSIN  \
                           << m_active_jointsSIN

#define OUTPUT_SIGNALS        m_tau_desSOUT \
                           << m_f_des_right_footSOUT \
                           << m_f_des_left_footSOUT \
                           << m_zmp_des_right_footSOUT \
                           << m_zmp_des_left_footSOUT \
                           << m_zmp_des_right_foot_localSOUT \
                           << m_zmp_des_left_foot_localSOUT \
                           << m_zmp_desSOUT \
                           << m_zmp_right_footSOUT \
                           << m_zmp_left_footSOUT \
                           << m_zmpSOUT \
                           << m_comSOUT \
                           << m_com_velSOUT \
                           << m_base_orientationSOUT \
                           << m_right_foot_posSOUT \
                           << m_left_foot_posSOUT \
                           << m_dv_desSOUT

      /// Define EntityClassName here rather than in the header file
      /// so that it can be used by the macros DEFINE_SIGNAL_**_FUNCTION.
      typedef InverseDynamicsBalanceController EntityClassName;

      /* --- DG FACTORY ---------------------------------------------------- */
      DYNAMICGRAPH_FACTORY_ENTITY_PLUGIN(InverseDynamicsBalanceController,
                                         "InverseDynamicsBalanceController");

      /* ------------------------------------------------------------------- */
      /* --- CONSTRUCTION -------------------------------------------------- */
      /* ------------------------------------------------------------------- */
      InverseDynamicsBalanceController::
          InverseDynamicsBalanceController(const std::string& name)
            : Entity(name)
            ,CONSTRUCT_SIGNAL_IN(com_ref_pos,                 ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(com_ref_vel,                 ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(com_ref_acc,                 ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(posture_ref_pos,             ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(posture_ref_vel,             ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(posture_ref_acc,             ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(base_orientation_ref_pos,    ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(base_orientation_ref_vel,    ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(base_orientation_ref_acc,    ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(kp_base_orientation,         ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(kd_base_orientation,         ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(kp_constraints,              ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(kd_constraints,              ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(kp_com,                      ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(kd_com,                      ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(kp_posture,                  ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(kd_posture,                  ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(kp_pos,                      ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(kd_pos,                      ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(w_com,                       double)
            ,CONSTRUCT_SIGNAL_IN(w_posture,                   double)
            ,CONSTRUCT_SIGNAL_IN(w_base_orientation,          double)
            ,CONSTRUCT_SIGNAL_IN(w_torques,                   double)
            ,CONSTRUCT_SIGNAL_IN(w_forces,                    double)
            ,CONSTRUCT_SIGNAL_IN(weight_contact_forces,       ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(mu,                          double)
            ,CONSTRUCT_SIGNAL_IN(contact_points,              ml::Matrix)
            ,CONSTRUCT_SIGNAL_IN(contact_normal,              ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(f_min,                       double)
            ,CONSTRUCT_SIGNAL_IN(rotor_inertias,              ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(gear_ratios,                 ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(tau_max,                     ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(q_min,                       ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(q_max,                       ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(dq_max,                      ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(ddq_max,                     ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(dt_joint_pos_limits,         double    )
            ,CONSTRUCT_SIGNAL_IN(tau_estimated,               ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(q,                           ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(v,                           ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(wrench_base,                 ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(wrench_left_foot,            ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(wrench_right_foot,           ml::Vector)
            ,CONSTRUCT_SIGNAL_IN(active_joints,               ml::Vector)
            ,CONSTRUCT_SIGNAL_OUT(tau_des,                    ml::Vector, INPUT_SIGNALS)
            ,CONSTRUCT_SIGNAL_OUT(f_des_right_foot,           ml::Vector, m_tau_desSOUT)
            ,CONSTRUCT_SIGNAL_OUT(f_des_left_foot,            ml::Vector, m_tau_desSOUT)
            ,CONSTRUCT_SIGNAL_OUT(zmp_des_right_foot,         ml::Vector, m_f_des_right_footSOUT)
            ,CONSTRUCT_SIGNAL_OUT(zmp_des_left_foot,          ml::Vector, m_f_des_left_footSOUT)
            ,CONSTRUCT_SIGNAL_OUT(zmp_des_right_foot_local,   ml::Vector, m_f_des_right_footSOUT)
            ,CONSTRUCT_SIGNAL_OUT(zmp_des_left_foot_local,    ml::Vector, m_f_des_left_footSOUT)
            ,CONSTRUCT_SIGNAL_OUT(zmp_des,                    ml::Vector, m_zmp_des_left_footSOUT<<
                                                                          m_zmp_des_right_footSOUT)
            ,CONSTRUCT_SIGNAL_OUT(zmp_right_foot,             ml::Vector, m_wrench_right_footSIN)
            ,CONSTRUCT_SIGNAL_OUT(zmp_left_foot,              ml::Vector, m_wrench_left_footSIN)
            ,CONSTRUCT_SIGNAL_OUT(zmp,                        ml::Vector, m_wrench_left_footSIN<<
                                                                          m_wrench_right_footSIN<<
                                                                          m_zmp_left_footSOUT<<
                                                                          m_zmp_right_footSOUT)
            ,CONSTRUCT_SIGNAL_OUT(dv_des,                     ml::Vector, m_tau_desSOUT)
            ,CONSTRUCT_SIGNAL_OUT(com,                        ml::Vector, m_tau_desSOUT)
            ,CONSTRUCT_SIGNAL_OUT(com_vel,                    ml::Vector, m_tau_desSOUT)
            ,CONSTRUCT_SIGNAL_OUT(base_orientation,           ml::Vector, m_tau_desSOUT)
            ,CONSTRUCT_SIGNAL_OUT(left_foot_pos,              ml::Vector, m_tau_desSOUT)
            ,CONSTRUCT_SIGNAL_OUT(right_foot_pos,             ml::Vector, m_tau_desSOUT)
            ,CONSTRUCT_SIGNAL_INNER(active_joints_checked,    ml::Vector, m_active_jointsSIN)
            ,m_initSucceeded(false)
            ,m_enabled(false)
            ,m_t(0.0)
            ,m_firstTime(true)
            ,m_timeLast(0)
      {
        Entity::signalRegistration( INPUT_SIGNALS << OUTPUT_SIGNALS );

        m_zmp_des_RF.setZero();
        m_zmp_des_LF.setZero();
        m_zmp_des_RF_local.setZero();
        m_zmp_des_LF_local.setZero();
        m_zmp_des.setZero();
        m_zmp_RF.setZero();
        m_zmp_LF.setZero();
        m_zmp.setZero();

        /* Commands. */
        addCommand("init",
                   makeCommandVoid2(*this, &InverseDynamicsBalanceController::init,
                                    docCommandVoid2("Initialize the entity.",
                                                    "Time period in seconds (double)",
                                                    "URDF file path (string)")));
      }

      void InverseDynamicsBalanceController::init(const double& dt, const std::string& urdfFile)
      {
        if(dt<=0.0)
          return SEND_MSG("Init failed: Timestep must be positive", MSG_TYPE_ERROR);

        EIGEN_CONST_MATRIX_FROM_SIGNAL(contactPoints, m_contact_pointsSIN(0));
        assert(contactPoints.rows()==3 && contactPoints.cols()==4);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(contactNormal, m_contact_normalSIN(0));
        assert(contactNormal.size()==3);
//        EIGEN_CONST_VECTOR_FROM_SIGNAL(w_forceReg, m_weight_contact_forcesSIN(0));
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kp_contact, m_kp_constraintsSIN(0));
        assert(kp_contact.size()==6);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kd_contact, m_kd_constraintsSIN(0));
        assert(kd_contact.size()==6);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kp_com, m_kp_comSIN(0));
        assert(kp_com.size()==3);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kd_com, m_kd_comSIN(0));
        assert(kd_com.size()==3);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kp_posture, m_kp_postureSIN(0));
        assert(kp_posture.size()==N_JOINTS);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kd_posture, m_kd_postureSIN(0));
        assert(kd_posture.size()==N_JOINTS);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(rotor_inertias, m_rotor_inertiasSIN(0));
        assert(rotor_inertias.size()==N_JOINTS);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(gear_ratios, m_gear_ratiosSIN(0));
        assert(gear_ratios.size()==N_JOINTS);

        const double & w_com = m_w_comSIN(0);
        const double & w_posture = m_w_postureSIN(0);
//        const double & w_base_orientation = m_w_base_orientationSIN(0);
//        const double & w_torques = m_w_torquesSIN(0);
        const double & w_forces = m_w_forcesSIN(0);
        const double & mu = m_muSIN(0);
        const double & fMin = m_f_minSIN(0);

        try 
        {
          vector<string> package_dirs;
          m_robot = new RobotWrapper(urdfFile, package_dirs, se3::JointModelFreeFlyer());
          m_robot->rotor_inertias(rotor_inertias);
          m_robot->gear_ratios(gear_ratios);

          assert(m_robot->nv()-6==N_JOINTS);
          m_dv_sot.setZero(m_robot->nv());
          m_tau_sot.setZero(m_robot->nv()-6);
          m_f.setZero(24);
          m_q_urdf.setZero(m_robot->nq());
          m_v_urdf.setZero(m_robot->nv());
	  
	  m_invDyn = new InverseDynamicsFormulationAccForce("invdyn", *m_robot);

          m_contactRF = new Contact6d("contact_rfoot", *m_robot, RIGHT_FOOT_FRAME_NAME,
                                      contactPoints, contactNormal,
                                      mu, fMin, w_forces);
          m_contactRF->Kp(kp_contact);
          m_contactRF->Kd(kd_contact);
          m_invDyn->addRigidContact(*m_contactRF);

          m_contactLF = new Contact6d("contact_lfoot", *m_robot, LEFT_FOOT_FRAME_NAME,
                                      contactPoints, contactNormal,
                                      mu, fMin, w_forces);
          m_contactLF->Kp(kp_contact);
          m_contactLF->Kd(kd_contact);
          m_invDyn->addRigidContact(*m_contactLF);

          m_taskCom = new TaskComEquality("task-com", *m_robot);
          m_taskCom->Kp(kp_com);
          m_taskCom->Kd(kd_com);
          m_invDyn->addMotionTask(*m_taskCom, w_com, 1);

          m_taskPosture = new TaskJointPosture("task-posture", *m_robot);
          m_taskPosture->Kp(kp_posture);
          m_taskPosture->Kd(kd_posture);
          m_invDyn->addMotionTask(*m_taskPosture, w_posture, 1);

          m_sampleCom = TrajectorySample(3);
          m_samplePosture = TrajectorySample(m_robot->nv()-6);

          m_hqpSolver = Solver_HQP_base::getNewSolver(SOLVER_HQP_EIQUADPROG,
                                                      "solver-eiquadprog");
          m_hqpSolver->resize(m_invDyn->nVar(), m_invDyn->nEq(), m_invDyn->nIn());
        } 
        catch (const std::exception& e) 
        { 
          std::cout << e.what();
          return SEND_MSG("Init failed: Could load URDF :" + urdfFile, MSG_TYPE_ERROR);
        }
        m_dt = dt;
        m_initSucceeded = true;
      }

      /* ------------------------------------------------------------------- */
      /* --- SIGNALS --------ontact_normal----------------------------------------------- */
      /* ------------------------------------------------------------------- */
      /** Copy active_joints only if a valid transition occurs. (From all OFF) or (To all OFF)**/
      DEFINE_SIGNAL_INNER_FUNCTION(active_joints_checked, ml::Vector)
      {
        if(s.size()!=N_JOINTS)
          s.resize(N_JOINTS);

        EIGEN_CONST_VECTOR_FROM_SIGNAL(active_joints_sot, m_active_jointsSIN(iter));
        if (m_enabled == false)
        {
          if (active_joints_sot.any())
          {
              /* from all OFF to some ON */
              m_enabled = true ;
              EIGEN_VECTOR_TO_VECTOR(active_joints_sot, s);
              Eigen::VectorXd active_joints_urdf(N_JOINTS);
              joints_sot_to_urdf(active_joints_sot, active_joints_urdf);

              m_taskBlockedJoints = new TaskJointPosture("task-posture", *m_robot);
              Eigen::VectorXd blocked_joints(N_JOINTS);
              for(unsigned int i=0; i<N_JOINTS; i++)
                if(active_joints_urdf(i)==0.0)
                  blocked_joints(i) = 1.0;
                else
                  blocked_joints(i) = 0.0;
              SEND_MSG("Blocked joints: "+toString(blocked_joints.transpose()), MSG_TYPE_INFO);
              m_taskBlockedJoints->mask(blocked_joints);
              TrajectorySample ref_zero(N_JOINTS);
              m_taskBlockedJoints->setReference(ref_zero);
              m_invDyn->addMotionTask(*m_taskBlockedJoints, 1.0, 0);
          }
        }
        else if (!active_joints_sot.any())
        {
            /* from some ON to all OFF */
            m_enabled = false ;
        }
        if (m_enabled == false)
          for(int i=0; i<N_JOINTS; i++)
            s(i)=false;
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(tau_des,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal tau_des before initialization!");
          return s;
        }
        if(s.size()!=N_JOINTS)
          s.resize(N_JOINTS);

        getProfiler().start(PROFILE_TAU_DES_COMPUTATION);

        getProfiler().start(PROFILE_READ_INPUT_SIGNALS);
        m_active_joints_checkedSINNER(iter);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(q_sot, m_qSIN(iter));
        assert(q_sot.size()==N_JOINTS+6);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(v_sot, m_vSIN(iter));
        assert(v_sot.size()==N_JOINTS+6);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(x_com_ref,   m_com_ref_posSIN(iter));
        assert(x_com_ref.size()==3);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(dx_com_ref,  m_com_ref_velSIN(iter));
        assert(dx_com_ref.size()==3);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(ddx_com_ref, m_com_ref_accSIN(iter));
        assert(ddx_com_ref.size()==3);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(q_ref,   m_posture_ref_posSIN(iter));
        assert(q_ref.size()==N_JOINTS);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(dq_ref,  m_posture_ref_velSIN(iter));
        assert(dq_ref.size()==N_JOINTS);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(ddq_ref, m_posture_ref_accSIN(iter));
        assert(ddq_ref.size()==N_JOINTS);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kp_contact, m_kp_constraintsSIN(iter));
        assert(kp_contact.size()==6);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kd_contact, m_kd_constraintsSIN(iter));
        assert(kd_contact.size()==6);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kp_com, m_kp_comSIN(iter));
        assert(kp_com.size()==3);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kd_com, m_kd_comSIN(iter));
        assert(kd_com.size()==3);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kp_posture, m_kp_postureSIN(iter));
        assert(kp_posture.size()==N_JOINTS);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kd_posture, m_kd_postureSIN(iter));
        assert(kd_posture.size()==N_JOINTS);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kp_pos, m_kp_posSIN(iter));
        assert(kp_pos.size()==N_JOINTS);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(kd_pos, m_kd_posSIN(iter));
        assert(kd_pos.size()==N_JOINTS);
        getProfiler().stop(PROFILE_READ_INPUT_SIGNALS);

        getProfiler().start(PROFILE_PREPARE_INV_DYN);
        config_sot_to_urdf(q_sot, m_q_urdf);
        velocity_sot_to_urdf(v_sot, m_v_urdf);

        m_sampleCom.pos = x_com_ref;
        m_sampleCom.vel = dx_com_ref;
        m_sampleCom.acc = ddx_com_ref;
        joints_sot_to_urdf(q_ref, m_samplePosture.pos);
        joints_sot_to_urdf(dq_ref, m_samplePosture.vel);
        joints_sot_to_urdf(ddq_ref, m_samplePosture.acc);

        m_taskCom->setReference(m_sampleCom);
        m_taskPosture->setReference(m_samplePosture);

        m_taskCom->Kp(kp_com);
        m_taskCom->Kd(kd_com);
        m_taskPosture->Kp(kp_posture);
        m_taskPosture->Kd(kd_posture);
        m_contactLF->Kp(kp_contact);
        m_contactLF->Kd(kd_contact);
        m_contactRF->Kp(kp_contact);
        m_contactRF->Kd(kd_contact);

        if(m_firstTime)
        {
          m_firstTime = false;
          m_invDyn->computeProblemData(m_t, m_q_urdf, m_v_urdf);
//          m_robot->computeAllTerms(m_invDyn->data(), q, v);
          se3::SE3 H_lf = m_robot->position(m_invDyn->data(),
                                            m_robot->model().getJointId(LEFT_FOOT_FRAME_NAME));
          m_contactLF->setReference(H_lf);
          SEND_MSG("Setting left foot reference to "+toString(H_lf), MSG_TYPE_DEBUG);

          se3::SE3 H_rf = m_robot->position(m_invDyn->data(),
                                            m_robot->model().getJointId(RIGHT_FOOT_FRAME_NAME));
          m_contactRF->setReference(H_rf);
          SEND_MSG("Setting right foot reference to "+toString(H_rf), MSG_TYPE_DEBUG);
        }
        else if(m_timeLast != iter-1)
        {
          SEND_MSG("Last time "+toString(m_timeLast)+" is not current time-1: "+toString(iter), MSG_TYPE_ERROR);
          if(m_timeLast == iter)
          {
            EIGEN_VECTOR_TO_VECTOR(m_tau_sot, s);
            return s;
          }
        }
        m_timeLast = iter;

        const HqpData & hqpData = m_invDyn->computeProblemData(m_t, m_q_urdf, m_v_urdf);
        getProfiler().stop(PROFILE_PREPARE_INV_DYN);

        getProfiler().start(PROFILE_HQP_SOLUTION);
        const HqpOutput & sol = m_hqpSolver->solve(hqpData);
        getProfiler().stop(PROFILE_HQP_SOLUTION);

        if(sol.status!=HQP_STATUS_OPTIMAL)
        {
          SEND_ERROR_STREAM_MSG("HQP solver failed to find a solution: "+toString(sol.status));
          SEND_MSG("HQP solver failed to find a solution: "+toString(sol.status), MSG_TYPE_ERROR);
          SEND_MSG(hqpDataToString(hqpData, false), MSG_TYPE_DEBUG);
          s.resize(0);
          return s;
        }

        velocity_urdf_to_sot(sol.x.head(m_robot->nv()), m_dv_sot);
        m_f = sol.x.tail(24);
        joints_urdf_to_sot(m_invDyn->computeActuatorForces(sol), m_tau_sot);

        m_tau_sot += kp_pos.cwiseProduct(q_ref-q_sot.tail<N_JOINTS>()) +
                     kd_pos.cwiseProduct(dq_ref-v_sot.tail<N_JOINTS>());

        getProfiler().stop(PROFILE_TAU_DES_COMPUTATION);
        m_t += m_dt;

        EIGEN_VECTOR_TO_VECTOR(m_tau_sot, s);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(dv_des,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal dv_des before initialization!");
          return s;
        }
        if(s.size()!=m_robot->nv())
          s.resize(m_robot->nv());
        m_tau_desSOUT(iter);
        EIGEN_VECTOR_TO_VECTOR(m_dv_sot, s);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(f_des_right_foot,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal f_des_right_foot before initialization!");
          return s;
        }
        if(s.size()!=6)
          s.resize(6);
        m_tau_desSOUT(iter);
        const Eigen::MatrixXd & T = m_contactRF->getForceGeneratorMatrix();
        m_f_RF = T*m_f.head<12>();
        EIGEN_VECTOR_TO_VECTOR(m_f_RF, s);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(f_des_left_foot,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal f_des_left_foot before initialization!");
          return s;
        }
        if(s.size()!=6)
          s.resize(6);
        m_tau_desSOUT(iter);
        const Eigen::MatrixXd & T = m_contactLF->getForceGeneratorMatrix();
        m_f_LF = T*m_f.tail<12>();
        EIGEN_VECTOR_TO_VECTOR(m_f_LF, s);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(zmp_des_right_foot_local,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal zmp_des_right_foot_local before initialization!");
          return s;
        }
        if(s.size()!=2)
          s.resize(2);

        m_f_des_right_footSOUT(iter);
        if(fabs(m_f_RF(2)>1.0))
        {
          m_zmp_des_RF_local(0) = -m_f_RF(4) / m_f_RF(2);
          m_zmp_des_RF_local(1) =  m_f_RF(3) / m_f_RF(2);
          m_zmp_des_RF_local(2) = 0.0;
        }
        else
          m_zmp_des_RF_local.setZero();

        s(0) = m_zmp_des_RF_local(0);
        s(1) = m_zmp_des_RF_local(1);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(zmp_des_left_foot_local,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal zmp_des_left_foot_local before initialization!");
          return s;
        }
        if(s.size()!=2)
          s.resize(2);
        m_f_des_left_footSOUT(iter);
        if(fabs(m_f_LF(2)>1.0))
        {
          m_zmp_des_LF_local(0) = -m_f_LF(4) / m_f_LF(2);
          m_zmp_des_LF_local(1) =  m_f_LF(3) / m_f_LF(2);
          m_zmp_des_LF_local(2) = 0.0;
        }
        else
          m_zmp_des_LF_local.setZero();

        s(0) = m_zmp_des_LF_local(0);
        s(1) = m_zmp_des_LF_local(1);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(zmp_des_right_foot,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal zmp_des_right_foot before initialization!");
          return s;
        }
        if(s.size()!=2)
          s.resize(2);
        m_f_des_right_footSOUT(iter);
        se3::SE3 H_rf = m_robot->position(m_invDyn->data(),
                                          m_robot->model().getJointId(RIGHT_FOOT_FRAME_NAME));
        if(fabs(m_f_RF(2)>1.0))
        {
          m_zmp_des_RF(0) = -m_f_RF(4) / m_f_RF(2);
          m_zmp_des_RF(1) =  m_f_RF(3) / m_f_RF(2);
          m_zmp_des_RF(2) = -H_rf.translation()(2);
        }
        else
          m_zmp_des_RF.setZero();

        m_zmp_des_RF = H_rf.act(m_zmp_des_RF);
        s(0) = m_zmp_des_RF(0);
        s(1) = m_zmp_des_RF(1);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(zmp_des_left_foot,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal zmp_des_left_foot before initialization!");
          return s;
        }
        if(s.size()!=2)
          s.resize(2);
        m_f_des_left_footSOUT(iter);
        se3::SE3 H_lf = m_robot->position(m_invDyn->data(),
                                          m_robot->model().getJointId(LEFT_FOOT_FRAME_NAME));
        if(fabs(m_f_LF(2)>1.0))
        {
          m_zmp_des_LF(0) = -m_f_LF(4) / m_f_LF(2);
          m_zmp_des_LF(1) =  m_f_LF(3) / m_f_LF(2);
          m_zmp_des_LF(2) = -H_lf.translation()(2);
        }
        else
          m_zmp_des_LF.setZero();

        m_zmp_des_LF = H_lf.act(m_zmp_des_LF);
        s(0) = m_zmp_des_LF(0);
        s(1) = m_zmp_des_LF(1);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(zmp_des, ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal zmp_des before initialization!");
          return s;
        }
        if(s.size()!=2)
          s.resize(2);
        m_zmp_des_left_footSOUT(iter);
        m_zmp_des_right_footSOUT(iter);

        m_zmp_des = (m_f_RF(2)*m_zmp_des_RF + m_f_LF(2)*m_zmp_des_LF) / (m_f_LF(2)+m_f_RF(2));
        s(0) = m_zmp_des(0);
        s(1) = m_zmp_des(1);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(zmp_right_foot,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal zmp_right_foot before initialization!");
          return s;
        }
        if(s.size()!=2)
          s.resize(2);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(f, m_wrench_right_footSIN(iter));
        assert(f.size()==6);
        se3::SE3 H_rf = m_robot->position(m_invDyn->data(),
                                          m_robot->model().getJointId(RIGHT_FOOT_FRAME_NAME));
        if(fabs(f(2)>1.0))
        {
          m_zmp_RF(0) = -f(4) / f(2);
          m_zmp_RF(1) =  f(3) / f(2);
          m_zmp_RF(2) = -H_rf.translation()(2);
        }
        else
          m_zmp_RF.setZero();

        m_zmp_RF = H_rf.act(m_zmp_RF);
        s(0) = m_zmp_RF(0);
        s(1) = m_zmp_RF(1);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(zmp_left_foot,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal zmp_left_foot before initialization!");
          return s;
        }
        if(s.size()!=2)
          s.resize(2);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(f, m_wrench_left_footSIN(iter));
        assert(f.size()==6);
        se3::SE3 H_lf = m_robot->position(m_invDyn->data(),
                                          m_robot->model().getJointId(LEFT_FOOT_FRAME_NAME));
        if(fabs(f(2)>1.0))
        {
          m_zmp_LF(0) = -f(4) / f(2);
          m_zmp_LF(1) =  f(3) / f(2);
          m_zmp_LF(2) = -H_lf.translation()(2);
        }
        else
          m_zmp_LF.setZero();

        m_zmp_LF = H_lf.act(m_zmp_LF);
        s(0) = m_zmp_LF(0);
        s(1) = m_zmp_LF(1);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(zmp, ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal zmp before initialization!");
          return s;
        }
        if(s.size()!=2)
          s.resize(2);
        EIGEN_CONST_VECTOR_FROM_SIGNAL(f_LF, m_wrench_left_footSIN(iter));
        EIGEN_CONST_VECTOR_FROM_SIGNAL(f_RF, m_wrench_right_footSIN(iter));
        m_zmp_left_footSOUT(iter);
        m_zmp_right_footSOUT(iter);

        if(f_LF(2)+f_RF(2) > 1.0)
          m_zmp = (f_RF(2)*m_zmp_RF + f_LF(2)*m_zmp_LF) / (f_LF(2)+f_RF(2));
        s(0) = m_zmp(0);
        s(1) = m_zmp(1);
        return s;
      }
      
      
      DEFINE_SIGNAL_OUT_FUNCTION(com,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal com before initialization!");
          return s;
        }
        if(s.size()!=3)
          s.resize(3);
        const Vector3 & com = m_robot->com(m_invDyn->data());
        EIGEN_VECTOR_TO_VECTOR(com, s);
        return s;
      }

      DEFINE_SIGNAL_OUT_FUNCTION(com_vel,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal com_vel before initialization!");
          return s;
        }
        if(s.size()!=3)
          s.resize(3);
        const Vector3 & com_vel = m_robot->com_vel(m_invDyn->data());
        EIGEN_VECTOR_TO_VECTOR(com_vel, s);
        return s;
      }
      
      
      DEFINE_SIGNAL_OUT_FUNCTION(base_orientation,ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal base_orientation before initialization!");
          return s;
        }
        /*
         * Code
         */
        return s;
      }
      
      DEFINE_SIGNAL_OUT_FUNCTION(left_foot_pos, ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal left_foot_pos before initialization!");
          return s;
        }
        /*
         * Code
         */
        return s;
      }
      
      DEFINE_SIGNAL_OUT_FUNCTION(right_foot_pos, ml::Vector)
      {
        if(!m_initSucceeded)
        {
          SEND_WARNING_STREAM_MSG("Cannot compute signal right_foot_pos before initialization!");
          return s;
        }
        /*
         * Code
         */
        return s;
      }


      /* --- COMMANDS ---------------------------------------------------------- */

      /* ------------------------------------------------------------------- */
      /* --- ENTITY -------------------------------------------------------- */
      /* ------------------------------------------------------------------- */

      void InverseDynamicsBalanceController::display(std::ostream& os) const
      {
        os << "InverseDynamicsBalanceController "<<getName();
        try
        {
          getProfiler().report_all(3, os);
        }
        catch (ExceptionSignal e) {}
      }

      void InverseDynamicsBalanceController::commandLine(const std::string& cmdLine,
                                            std::istringstream& cmdArgs,
                                            std::ostream& os )
      {
        if( cmdLine == "help" )
        {
          os << "InverseDynamicsBalanceController:\n"
              << "\t -." << std::endl;
          Entity::commandLine(cmdLine, cmdArgs, os);
        }
        else
        {
          Entity::commandLine(cmdLine,cmdArgs,os);
        }
      }
      
    } // namespace torquecontrol
  } // namespace sot
} // namespace dynamicgraph

