/* Copyright (C) 2019
* All Rights Reserved.
*
* Authors: Xinyi Luo
* Date:2019-06
*/

#include "sqphot/SqpAlgorithm.hpp"
#include "sqphot/MessageHandling.hpp"

using namespace std;
using namespace Ipopt;

namespace SQPhotstart {

DECLARE_STD_EXCEPTION(NEW_POINTS_WITH_INCREASE_OBJ_ACCEPTED);
DECLARE_STD_EXCEPTION(SMALL_TRUST_REGION);

/**
 * Default Constructor
 */
SqpAlgorithm::SqpAlgorithm()
 : Active_Set_constraints_(NULL)
 , Active_Set_bounds_(NULL)
 , cons_type_(NULL)
 , bound_cons_type_(NULL)
{
  // Some of the following code was taking from the Ipopt code.

  ////////////////////////////////////////////////////////////////////////////
  //              Set up output and options handling.                       //
  ////////////////////////////////////////////////////////////////////////////
  // Create journalist and set it up so that it prints to stdout.
  jnlst_ = new Journalist();
  SmartPtr<Journal> stdout_jrnl =
      jnlst_->AddFileJournal("console", "stdout", J_ITERSUMMARY);
  stdout_jrnl->SetPrintLevel(J_DBG, J_NONE);

  // Create a new options list
  options_ = new OptionsList();

  // Get the list of registered options to define the options for the
  // SQP algorithm .*/
  SmartPtr<RegisteredOptions> reg_options = new RegisteredOptions();

  // Options related to output
  reg_options->SetRegisteringCategory("Output");
  reg_options->AddBoundedIntegerOption(
      "print_level", "Output verbosity level.", 0, J_LAST_LEVEL - 1,
      J_ITERSUMMARY, "Sets the default verbosity level for console output. The "
                     "larger this value the more detailed is the output.");

  reg_options->AddStringOption1(
      "output_file",
      "File name of desired output file (leave unset for no file output).", "",
      "*", "Any acceptable standard file name",
      "NOTE: This option only works when read from the sqp.opt options file! "
      "An output file with this name will be written (leave unset for no "
      "file output).  The verbosity level is by default set to "
      "\"print_level\", "
      "but can be overridden with \"file_print_level\".  The file name is "
      "changed to use only small letters.");
  reg_options->AddBoundedIntegerOption(
      "file_print_level", "Verbosity level for output file.", 0,
      J_LAST_LEVEL - 1, J_ITERSUMMARY,
      "NOTE: This option only works when read from the sqp.opt options file! "
      "Determines the verbosity level for the file specified by "
      "\"output_file\".  By default it is the same as \"print_level\".");

  // Set the algorithm specific options
  register_options_(reg_options);

  // Finalize options list by giving it the list of registered options
  // and the journalist (for error message).
  options_->SetJournalist(jnlst_);
  options_->SetRegisteredOptions(reg_options);
}

/**
 * Default Destructor
 */
SqpAlgorithm::~SqpAlgorithm()
{

  delete[] cons_type_;
  cons_type_ = NULL;
  delete[] bound_cons_type_;
  bound_cons_type_ = NULL;
  delete[] Active_Set_bounds_;
  Active_Set_bounds_ = NULL;
  delete[] Active_Set_constraints_;
  Active_Set_constraints_ = NULL;
}

/**
 * @brief This is the main function to optimize the NLP given as the input
 *
 * @param nlp: the nlp reader that read data of the function to be minimized;
 */
void SqpAlgorithm::Optimize()
{
  // Initalize the time at beginning of the algorithm
  cpu_time_at_start_ = get_cpu_time_since_start();
  wallclock_time_at_start_ = get_wallclock_time_since_start();

  // Initialize exit flag to UNKOWN to indicate that loop is not finished
  exit_flag_ = UNKNOWN;
  while (stats_->iter < max_num_iterations_ && exit_flag_ == UNKNOWN) {
    setupQP();
    // for debugging
    //@{
    //    hessian_->print_full("hessian");
    //    jacobian_->print_full("jacobian");
    //@}
    try {
      myQP_->solveQP(stats_,
                     options_); // solve the QP subproblem and update the stats_
    } catch (QP_NOT_OPTIMAL) {
      myQP_->WriteQPData(problem_name_ + "qpdata.log");
      exit_flag_ = myQP_->get_status();
      // break;
    }

    // get the search direction from the solution of the QPsubproblem
    get_search_direction();
    //        p_k_->print("p_k_");
    qp_obj_ = get_obj_QP();

    // Update the penalty parameter if necessary
    update_penalty_parameter();

    // calculate the infinity norm of the search direction
    norm_p_k_ = p_k_->calc_inf_norm();

    get_trial_point_info();

    ratio_test();

    // Calculate the second-order-correction steps
    second_order_correction();

    // Update the radius and the QP bounds if the radius has been changed
    stats_->iter_addone();
    /* output some information to the console*/

    // check if the current iterates is optimal and decide to
    // exit the loop or not
    if (print_level_ >= 2) {
      if (stats_->iter % 10 == 0) {
        jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_HEADER);
        jnlst_->Printf(J_ITERSUMMARY, J_MAIN, DOUBLE_LONG_DIVIDER);
      }
      jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_OUTPUT);
    } else {
      jnlst_->DeleteAllJournals();
      SmartPtr<Journal> logout_jrnl = jnlst_->GetJournal("file_output");
      if (IsNull(logout_jrnl)) {
        jnlst_->AddFileJournal("file_output", problem_name_ + "_output.log",
                               J_ITERSUMMARY);
      }
      if (IsValid(logout_jrnl)) {
        logout_jrnl->SetPrintLevel(J_STATISTICS, J_NONE);
      }
      if (stats_->iter % 10 == 0) {
        jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_HEADER);
        jnlst_->Printf(J_ITERSUMMARY, J_MAIN, DOUBLE_LONG_DIVIDER);
      }
      jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_OUTPUT);
    }

    check_optimality();
    if (exit_flag_ != UNKNOWN) {
      break;
    }

    try {
      update_radius();
    } catch (SMALL_TRUST_REGION) {
      check_optimality();
      break;
    }
  }

  // check if the current iterates get_status before exiting
  if (stats_->iter == max_num_iterations_)
    exit_flag_ = EXCEED_MAX_ITERATIONS;

  // check if we are running out of CPU time
  double current_cpu_time = get_cpu_time_since_start();
  if (current_cpu_time - cpu_time_at_start_ > cpu_time_limit_) {
    exit_flag_ = EXCEED_MAX_CPU_TIME;
  }

  // check if we are running out of wallclock time
  double current_wallclock_time = get_wallclock_time_since_start();
  if (current_wallclock_time - wallclock_time_at_start_ >
      wallclock_time_limit_) {
    exit_flag_ = EXCEED_MAX_WALLCLOCK_TIME;
  }

  //    if (exitflag_ != OPTIMAL && exitflag_ != INVALID_NLP) {
  //        check_optimality();
  //    }

  // print the final summary message to the console
  print_final_stats();
  jnlst_->FlushBuffer();
}

/**
 * @brief This is the function that checks if the current point is optimal, and
 * decides if to exit the loop or not
 * *@return if it decides the function is optimal, the class member _exitflag =
 * OPTIMAL
 * if it decides that there is an error during the function run or the
 *  function cannot be solved, it will assign _exitflag the	corresponding
 *  code according to the error type.
 */
void SqpAlgorithm::check_optimality()
{
  // FIXME: not sure if it is better to use the new multiplier or the old one

  double primal_violation = 0;
  double dual_violation = 0;
  double compl_violation = 0;
  double stationarity_violation = 0;

  int i;
  get_multipliers();

  if (multiplier_vars_ == nullptr)
    /**-------------------------------------------------------**/
    /**               Check the KKT conditions                **/
    /**-------------------------------------------------------**/

    /**-------------------------------------------------------**/
    /**                    Identify Active Set                **/
    /**-------------------------------------------------------**/
    for (i = 0; i < nCon_; i++) {
      if (cons_type_[i] == BOUNDED_ABOVE) {
        if (abs(c_u_->get_value(i) - c_k_->get_value(i)) < active_set_tol_)
          Active_Set_constraints_[i] = ACTIVE_ABOVE;
      } else if (cons_type_[i] == BOUNDED_BELOW) {
        if (abs(c_k_->get_value(i) - c_l_->get_value(i)) < active_set_tol_) {
          Active_Set_constraints_[i] = ACTIVE_BELOW;
        }
      } else if (cons_type_[i] == EQUAL) {
        if ((abs(c_u_->get_value(i) - c_k_->get_value(i)) < active_set_tol_) &&
            (abs(c_k_->get_value(i) - c_l_->get_value(i)) < active_set_tol_))
          Active_Set_constraints_[i] = ACTIVE_BOTH_SIDE;
      } else {
        Active_Set_constraints_[i] = INACTIVE;
      }
    }

  for (i = 0; i < nVar_; i++) {
    if (bound_cons_type_[i] == BOUNDED_ABOVE) {
      if (abs(x_u_->get_value(i) - x_k_->get_value(i)) < active_set_tol_)
        Active_Set_bounds_[i] = ACTIVE_ABOVE;
    } else if (bound_cons_type_[i] == BOUNDED_BELOW) {
      if (abs(x_k_->get_value(i) - x_l_->get_value(i)) < active_set_tol_)
        Active_Set_bounds_[i] = ACTIVE_BELOW;
    } else if (bound_cons_type_[i] == EQUAL) {
      if ((abs(x_u_->get_value(i) - x_k_->get_value(i)) < active_set_tol_) &&
          (abs(x_k_->get_value(i) - x_l_->get_value(i)) < active_set_tol_))
        Active_Set_bounds_[i] = ACTIVE_BOTH_SIDE;
    } else {
      Active_Set_bounds_[i] = INACTIVE;
    }
  }
  /**-------------------------------------------------------**/
  /**                    Primal Feasibility                 **/
  /**-------------------------------------------------------**/

  primal_violation += infea_measure_;

  /**-------------------------------------------------------**/
  /**                    Dual Feasibility                   **/
  /**-------------------------------------------------------**/

  //    multiplier_vars_->print("multiplier_vars");
  //    multiplier_cons_->print("multiplier_cons");
  //    x_k_->print("x_k");
  //    x_l_->print("x_l");
  //    x_u_->print("x_u");
  //
  //    c_k_->print("c_k");
  //    c_l_->print("c_l");
  //    c_u_->print("c_u");
  i = 0;
  opt_status_.dual_feasibility = true;
  while (i < nVar_) {
    if (bound_cons_type_[i] == BOUNDED_ABOVE) {
      dual_violation += max(multiplier_vars_->get_value(i), 0.0);
    } else if (bound_cons_type_[i] == BOUNDED_BELOW) {
      dual_violation += -min(multiplier_vars_->get_value(i), 0.0);
    }
    i++;
  }

  i = 0;
  while (i < nCon_) {
    if (cons_type_[i] == BOUNDED_ABOVE) {
      dual_violation += max(multiplier_cons_->get_value(i), 0.0);
    } else if (cons_type_[i] == BOUNDED_BELOW) {
      dual_violation += -min(multiplier_cons_->get_value(i), 0.0);
    }
    i++;
  }

  /**-------------------------------------------------------**/
  /**                    Complemtarity                      **/
  /**-------------------------------------------------------**/
  //@{

  i = 0;
  while (i < nCon_) {
    if (cons_type_[i] == BOUNDED_ABOVE) {
      compl_violation += abs(multiplier_cons_->get_value(i) *
                             (c_u_->get_value(i) - c_k_->get_value(i)));
    } else if (cons_type_[i] == BOUNDED_BELOW) {
      compl_violation += abs(multiplier_cons_->get_value(i) *
                             (c_k_->get_value(i) - c_l_->get_value(i)));
    } else if (cons_type_[i] == UNBOUNDED) {
      compl_violation += abs(multiplier_cons_->get_value(i));
    }
    i++;
  }

  i = 0;
  while (i < nVar_) {
    if (bound_cons_type_[i] == BOUNDED_ABOVE) {
      compl_violation += abs(multiplier_vars_->get_value(i) *
                             (x_u_->get_value(i) - x_k_->get_value(i)));
    } else if (bound_cons_type_[i] == BOUNDED_BELOW) {
      compl_violation += abs(multiplier_vars_->get_value(i) *
                             (x_k_->get_value(i) - x_l_->get_value(i)));
    } else if (bound_cons_type_[i] == UNBOUNDED) {
      compl_violation += abs(multiplier_vars_->get_value(i));
    }
    i++;
  }
  //@{
  //    c_l_->print("c_l");
  //    c_u_->print("c_u");
  //    std::cout<<compl_violation<<std::endl;
  //    multiplier_vars_->print("multiplier_vars");
  //    multiplier_cons_->print("multiplier_constr");
  //@}

  //@}
  /**-------------------------------------------------------**/
  /**                    Stationarity                       **/
  /**-------------------------------------------------------**/
  //@{
  shared_ptr<Vector> difference = make_shared<Vector>(nVar_);
  // the difference of g-J^T y -\lambda
  //
  //    grad_f_->print("grad_f_");
  //
  //    jacobian_->print_full("jacobian");
  //
  //    multiplier_cons_->print("multiplier_cons_");
  //    multiplier_vars_->print("multiplier_vars_");
  jacobian_->multiply_transpose(multiplier_cons_, difference);
  difference->add_vector(1., multiplier_vars_);
  difference->add_vector(-1., grad_f_);

  stationarity_violation = difference->calc_one_norm();
  //@}

  /**-------------------------------------------------------**/
  /**             Decide if x_k is optimal                  **/
  /**-------------------------------------------------------**/

  opt_status_.dual_violation = dual_violation;
  opt_status_.primal_violation = primal_violation;
  opt_status_.compl_violation = compl_violation;
  opt_status_.stationarity_violation = stationarity_violation;
  opt_status_.KKT_error = dual_violation + primal_violation + compl_violation +
                          stationarity_violation;
  //    printf("primal_violation = %23.16e\n",primal_violation);
  //    printf("dual_violation = %23.16e\n",dual_violation);
  //    printf("compl_violation = %23.16e\n",compl_violation);
  //    printf("statioanrity_violation = %23.16e\n",statioanrity_violation);
  //    printf("KKT error = %23.16e\n", opt_status_.KKT_error);

  opt_status_.primal_feasibility =
      primal_violation < opt_tol_primal_feasibility_;
  opt_status_.dual_feasibility = dual_violation < opt_tol_dual_feasibility_;
  opt_status_.complementarity = compl_violation < opt_tol_complementarity_;
  opt_status_.stationarity =
      stationarity_violation < opt_tol_stationarity_feasibility_;

  if (opt_status_.primal_feasibility && opt_status_.dual_feasibility &&
      opt_status_.complementarity && opt_status_.stationarity) {
    opt_status_.first_order_opt = true;
    exit_flag_ = OPTIMAL;
  } else {
// if it is not optimal
#ifdef DEBUG
#ifdef CHECK_TERMINATION

    EJournalLevel debug_print_level = old_options_->debug_print_level;
    SmartPtr<Journal> debug_jrnl = jnlst_->GetJournal("Debug");
    if (IsNull(debug_jrnl)) {
      debug_jrnl = jnlst_->AddFileJournal("Debug", "debug.out", J_ITERSUMMARY);
    }
    debug_jrnl->SetAllPrintLevels(debug_print_level);
    debug_jrnl->SetPrintLevel(J_DBG, J_ALL);
    jnlst_->Printf(J_ALL, J_DBG, DOUBLE_DIVIDER);
    jnlst_->Printf(J_ALL, J_DBG, "           Iteration  %i\n", stats_->iter);
    jnlst_->Printf(J_ALL, J_DBG, DOUBLE_DIVIDER);
    grad_f_->print("grad_f", jnlst_, J_MOREDETAILED, J_DBG);
    jnlst_->Printf(J_ALL, J_DBG, SINGLE_DIVIDER);
    c_u_->print("c_u", jnlst_, J_MOREDETAILED, J_DBG);
    c_l_->print("c_l", jnlst_, J_MOREDETAILED, J_DBG);
    c_k_->print("c_k", jnlst_, J_MOREDETAILED, J_DBG);
    multiplier_cons_->print("multiplier_cons", jnlst_, J_MOREDETAILED, J_DBG);
    jnlst_->Printf(J_ALL, J_DBG, SINGLE_DIVIDER);
    x_u_->print("x_u", jnlst_, J_MOREDETAILED, J_DBG);
    x_l_->print("x_l", jnlst_, J_MOREDETAILED, J_DBG);
    x_k_->print("x_k", jnlst_, J_MOREDETAILED, J_DBG);
    multiplier_vars_->print("multiplier_vars", jnlst_, J_MOREDETAILED, J_DBG);
    jnlst_->Printf(J_ALL, J_DBG, SINGLE_DIVIDER);
    jacobian_->print_full("jacobian", jnlst_, J_MOREDETAILED, J_DBG);
    hessian_->print_full("hessian", jnlst_, J_MOREDETAILED, J_DBG);
    difference->print("stationarity gap", jnlst_, J_MOREDETAILED, J_DBG);
    jnlst_->Printf(J_ALL, J_DBG, SINGLE_DIVIDER);
    jnlst_->Printf(J_ALL, J_DBG, "Feasibility      ");
    jnlst_->Printf(J_ALL, J_DBG, "%i\n", opt_status_.primal_feasibility);
    jnlst_->Printf(J_ALL, J_DBG, "Dual Feasibility ");
    jnlst_->Printf(J_ALL, J_DBG, "%i\n", opt_status_.dual_feasibility);
    jnlst_->Printf(J_ALL, J_DBG, "Stationarity     ");
    jnlst_->Printf(J_ALL, J_DBG, "%i\n", opt_status_.stationarity);
    jnlst_->Printf(J_ALL, J_DBG, "Complementarity  ");
    jnlst_->Printf(J_ALL, J_DBG, "%i\n", opt_status_.complementarity);
    jnlst_->Printf(J_ALL, J_DBG, SINGLE_DIVIDER);

#endif
#endif
  }
}

void SqpAlgorithm::get_trial_point_info()
{

  x_trial_->set_to_sum_of_vectors(1., x_k_, 1., p_k_);

  // Calculate f_trial, c_trial and infea_measure_trial for the trial points
  // x_trial
  sqp_nlp_->eval_f(x_trial_, obj_value_trial_);
  sqp_nlp_->eval_constraints(x_trial_, c_trial_);

#ifdef NEW_FORMULATION
  infea_measure_trial_ = cal_infea(c_trial_, c_l_, c_u_, x_trial_, x_l_, x_u_);
#else
  infea_measure_trial_ = cal_infea(
      c_trial_, c_l_, c_u_); // calculate the infeasibility measure for x_k
#endif
}

/**
 * @brief This function shifts the initial starting point to be feasible to the
 * bound constraints
 * @param x initial starting point
 * @param x_l lower bound constraints
 * @param x_u upper bound constraints
 */
// AW: This is only used here, so we can make it invisible to the outside
static void shift_starting_point_(shared_ptr<Vector> x,
                                  shared_ptr<const Vector> x_l,
                                  shared_ptr<const Vector> x_u)
{
  for (int i = 0; i < x->get_dim(); i++) {
    assert(x_l->get_value(i) <= x_u->get_value(i));
    if (x_l->get_value(i) > x->get_value(i)) {
      x->set_value(i, x_l->get_value(i));
      //            x->set_value(i,
      //            x_l->value(i)+0.5*(x_u->value(i)-x_l->value(i)));
    } else if (x->get_value(i) > x_u->get_value(i)) {
      x->set_value(i, x_u->get_value(i));
      //           x->set_value(i,
      //           x_u->value(i)-0.5*(x_u->value(i)-x_l->value(i)));
    }
  }
}

/**
 *  @brief This function initializes the objects required by the SQP Algorithm,
 *  copies some parameters required by the algorithm, obtains the function
 *  information for the first QP.
 *
 */
void SqpAlgorithm::initialize(std::shared_ptr<SqpNlpBase> sqp_nlp,
                              const string& name)
{
  // Get the options values from the options object
  get_option_values_();

  std::size_t found = name.find_last_of("/\\");

  problem_name_ = name.substr(found + 1);

  allocate_memory(sqp_nlp);

  delta_ = trust_region_init_value_;
  rho_ = penalty_parameter_init_value_;
  norm_p_k_ = 0.0;

  /*-----------------------------------------------------*/
  /*         Get the nlp information                     */
  /*-----------------------------------------------------*/
  sqp_nlp_->get_bounds_info(x_l_, x_u_, c_l_, c_u_);
  sqp_nlp_->get_starting_point(x_k_, multiplier_cons_);

// shift starting point to satisfy the bound constraint
#ifndef NEW_FORMULATION
  shift_starting_point_(x_k_, x_l_, x_u_);
#endif
  sqp_nlp_->eval_f(x_k_, obj_value_);
  sqp_nlp_->eval_gradient(x_k_, grad_f_);
  sqp_nlp_->eval_constraints(x_k_, c_k_);
  sqp_nlp_->get_hessian_structure(x_k_, multiplier_cons_, hessian_);
  sqp_nlp_->eval_hessian(x_k_, multiplier_cons_, hessian_);
  sqp_nlp_->get_jacobian_structure(x_k_, jacobian_);
  sqp_nlp_->eval_jacobian(x_k_, jacobian_);
  classify_constraints_types();

#ifdef NEW_FORMULATION
  infea_measure_ =
      cal_infea(c_k_, c_l_, c_u_, x_k_, x_l_,
                x_u_); // calculate the infeasibility measure for x_k
#else
  infea_measure_ = cal_infea(
      c_k_, c_l_, c_u_); // calculate the infeasibility measure for x_k
#endif

  /*-----------------------------------------------------*/
  /*             JOURNAL INIT & OUTPUT                   */
  /*-----------------------------------------------------*/

  if (print_level_ > 1) {
    SmartPtr<Journal> stdout_jrnl =
        jnlst_->AddFileJournal("console", "stdout", J_ITERSUMMARY);
    if (IsValid(stdout_jrnl)) {
      // Set printlevel for stdout
      stdout_jrnl->SetAllPrintLevels((EJournalLevel)print_level_);
      stdout_jrnl->SetPrintLevel(J_DBG, J_NONE);
    }
    jnlst_->Printf(J_ITERSUMMARY, J_MAIN, DOUBLE_LONG_DIVIDER);
    jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_HEADER);
    jnlst_->Printf(J_ITERSUMMARY, J_MAIN, DOUBLE_LONG_DIVIDER);
    jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_OUTPUT);

  } else {
    string output_file_name = problem_name_ + "_output.log";
    SmartPtr<Journal> logout_jrnl = jnlst_->AddFileJournal(
        "file_output", output_file_name.c_str(), J_ITERSUMMARY);
    if (IsValid(logout_jrnl)) {
      logout_jrnl->SetPrintLevel(J_STATISTICS, J_NONE);
    }

    jnlst_->Printf(J_ITERSUMMARY, J_MAIN, DOUBLE_LONG_DIVIDER);
    jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_HEADER);
    jnlst_->Printf(J_ITERSUMMARY, J_MAIN, DOUBLE_LONG_DIVIDER);
    jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_OUTPUT);
  }

  //#ifdef DEBUG
  //    SmartPtr<Journal> debug_jrnl =
  //        jnlst_->AddFileJournal("Debug", problem_name_+"debug.log",
  //                               J_MOREDETAILED);
  //    debug_jrnl->SetPrintLevel(J_DBG, J_MOREDETAILED);
  //#endif
}

/**
 * @brief alloocate memory for class members.
 * This function initializes all the shared pointer which will be used in the
 * Algorithm::Optimize, and it copies all parameters that might be changed
 * during
 * the run of the function Algorithm::Optimize.
 *
 * @param nlp: the nlp reader that read data of the function to be minimized;
 */
void SqpAlgorithm::allocate_memory(std::shared_ptr<SqpNlpBase> sqp_nlp)
{
  sqp_nlp_ = sqp_nlp;
  std::shared_ptr<const SqpNlpSizeInfo> nlp_sizes =
      sqp_nlp_->get_problem_sizes();
  nVar_ = nlp_sizes->get_num_variables();
  nCon_ = nlp_sizes->get_num_constraints();
  cons_type_ = new ConstraintType[nCon_];
  bound_cons_type_ = new ConstraintType[nVar_];
  Active_Set_bounds_ = new ActiveType[nVar_];
  Active_Set_constraints_ = new ActiveType[nCon_];

  x_k_ = make_shared<Vector>(nVar_);
  x_trial_ = make_shared<Vector>(nVar_);
  p_k_ = make_shared<Vector>(nVar_);
  multiplier_cons_ = make_shared<Vector>(nCon_);
  multiplier_vars_ = make_shared<Vector>(nVar_);
  c_k_ = make_shared<Vector>(nCon_);
  c_trial_ = make_shared<Vector>(nCon_);
  x_l_ = make_shared<Vector>(nVar_);
  x_u_ = make_shared<Vector>(nVar_);
  c_l_ = make_shared<Vector>(nCon_);
  c_u_ = make_shared<Vector>(nCon_);
  grad_f_ = make_shared<Vector>(nVar_);

  jacobian_ = make_shared<SpTripletMat>(nlp_sizes->get_num_nonzeros_jacobian(),
                                        nCon_, nVar_, false);
  hessian_ = make_shared<SpTripletMat>(nlp_sizes->get_num_nonzeros_hessian(),
                                       nVar_, nVar_, true);
  stats_ = make_shared<Stats>();

  myQP_ = make_shared<QPhandler>(nlp_sizes, QP, jnlst_, options_);
  myLP_ = make_shared<QPhandler>(nlp_sizes, LP, jnlst_, options_);
}

/**
* @brief This function calculates the infeasibility for given x_k and c_k with
respect
* to their corresponding bounds
* @return infea_measure = ||-max(c_k-c_u),0||_1 +||-min(c_k-c_l),0||_1+
                  ||-max(x_k-x_u),0||_1 +||-min(x_k-x_l),0||_1
*/
double SqpAlgorithm::cal_infea(shared_ptr<const Vector> c_k,
                               shared_ptr<const Vector> c_l,
                               shared_ptr<const Vector> c_u,
                               shared_ptr<const Vector> x_k,
                               shared_ptr<const Vector> x_l,
                               shared_ptr<const Vector> x_u)
{
  double infea_measure = 0.0;
  for (int i = 0; i < c_k_->get_dim(); i++) {
    if (c_k->get_value(i) < c_l->get_value(i))
      infea_measure += (c_l->get_value(i) - c_k->get_value(i));
    else if (c_k->get_value(i) > c_u->get_value(i))
      infea_measure += (c_k->get_value(i) - c_u->get_value(i));
  }

  if (x_k != nullptr) {
    for (int i = 0; i < x_k_->get_dim(); i++) {
      if (x_k->get_value(i) < x_l->get_value(i))
        infea_measure += (x_l->get_value(i) - x_k->get_value(i));
      else if (x_k->get_value(i) > x_u->get_value(i))
        infea_measure += (x_k->get_value(i) - x_u->get_value(i));
    }
  }

  return infea_measure;
}

/**
 * @brief This function extracts the search direction for NLP from the QP
 * subproblem
 * solved, and copies it to the class member _p_k
 */
void SqpAlgorithm::get_search_direction()
{
  p_k_->copy_values(myQP_->get_optimal_solution()->get_values());
}

/**
 * @brief This function gets the Lagragian multipliers for constraints and for
 * for the
 * bounds from QPsolver
 */

void SqpAlgorithm::get_multipliers()
{
  if (qp_solver_choice_ == QORE || qp_solver_choice_ == QPOASES) {
    multiplier_cons_->copy_vector(myQP_->get_constraints_multipliers());
    multiplier_vars_->copy_values(
        myQP_->get_bounds_multipliers()->get_values()); // AW would be good to
    // separate different parts
    // of the QP multipliers?
  } else if (qp_solver_choice_ == GUROBI || qp_solver_choice_ == CPLEX) {
    multiplier_cons_->copy_vector(myQP_->get_constraints_multipliers());
    shared_ptr<Vector> tmp_vec_nVar = make_shared<Vector>(nVar_);
    jacobian_->multiply_transpose(multiplier_cons_, tmp_vec_nVar);
    hessian_->multiply(p_k_, multiplier_vars_);
    multiplier_vars_->add_vector(1., grad_f_);
    multiplier_vars_->add_vector(-1., tmp_vec_nVar);
  }
}

DECLARE_STD_EXCEPTION(QP_UNCHANGED);

/**
 * @brief This function will set up the data for the QP subproblem
 *
 * It will initialize all the data at once at the beginning of the Algorithm.
 * After
 * that, the data in the QP problem will be updated according to the class
 * member QPinfoFlag_
 */

void SqpAlgorithm::setupQP()
{
  if (stats_->iter == 0) {
    myQP_->set_A(jacobian_);
    myQP_->set_H(hessian_);
    myQP_->set_bounds(delta_, x_l_, x_u_, x_k_, c_l_, c_u_, c_k_);
    myQP_->set_g(grad_f_, rho_);
  } else {
    if ((!QPinfoFlag_.Update_g) && (!QPinfoFlag_.Update_H) &&
        (!QPinfoFlag_.Update_A) && (!QPinfoFlag_.Update_bounds) &&
        (!QPinfoFlag_.Update_delta) && (!QPinfoFlag_.Update_penalty)) {

      auto stdout_jrnl = jnlst_->GetJournal("console");

      if (IsNull(stdout_jrnl)) {
        SmartPtr<Journal> stdout_jrnl =
            jnlst_->AddFileJournal("console", "stdout", J_ITERSUMMARY);
      }
      if (IsValid(stdout_jrnl)) {
        // Set printlevel for stdout
        stdout_jrnl->SetAllPrintLevels((EJournalLevel)print_level_);
        stdout_jrnl->SetPrintLevel(J_DBG, J_NONE);
      }

      jnlst_->Printf(J_WARNING, J_MAIN, "QP is not changed!");
      THROW_EXCEPTION(QP_UNCHANGED, "QP is not changed");
    }
    if (QPinfoFlag_.Update_A) {
      myQP_->update_A(jacobian_);
      QPinfoFlag_.Update_A = false;
    }
    if (QPinfoFlag_.Update_H) {
      myQP_->update_H(hessian_);
      QPinfoFlag_.Update_H = false;
    }
    if (QPinfoFlag_.Update_bounds) {
      myQP_->update_bounds(delta_, x_l_, x_u_, x_k_, c_l_, c_u_, c_k_);
      QPinfoFlag_.Update_bounds = false;
      QPinfoFlag_.Update_delta = false;
    } else if (QPinfoFlag_.Update_delta) {
      myQP_->update_delta(delta_, x_l_, x_u_, x_k_);
      QPinfoFlag_.Update_delta = false;
    }
    if (QPinfoFlag_.Update_penalty) {
      myQP_->update_penalty(rho_);
      QPinfoFlag_.Update_penalty = false;
    }
    if (QPinfoFlag_.Update_g) {
      myQP_->update_grad(grad_f_);
      QPinfoFlag_.Update_g = false;
    }
  }
}

void SqpAlgorithm::setupLP()
{
  myLP_->set_bounds(delta_, x_l_, x_u_, x_k_, c_l_, c_u_, c_k_);
  myLP_->set_g(rho_);
  myLP_->set_A(jacobian_);
}

/**
 *
 * @brief This function performs the ratio test to determine if we should accept
 * the trial point
 *
 * The ratio is calculated by
 * (P_1(x_k;\rho)-P_1( x_trial;\rho))/(q_k(0;\rho)-q_k(p_k;rho), where
 * P_1(x,rho) = f(x) + rho* infeasibility_measure is the l_1 merit function and
 * q_k(p; rho) = f_k+ g_k^Tp +1/2 p^T H_k p+rho* infeasibility_measure_model is
 * the
 * quadratic model at x_k.
 * The trial point  will be accepted if the ratio >=
 * trust_region_ratio_accept_tol.
 * If it is accepted, the function will also updates the gradient, Jacobian
 * information by reading from nlp_ object. The corresponding flags of class
 * member
 * QPinfoFlag_ will set to be true.
 */
void SqpAlgorithm::ratio_test()
{

  double P1_x = obj_value_ + rho_ * infea_measure_;
  double P1_x_trial = obj_value_trial_ + rho_ * infea_measure_trial_;

  actual_reduction_ = P1_x - P1_x_trial;
  pred_reduction_ = rho_ * infea_measure_ - get_obj_QP();

#ifdef DEBUG
#ifdef CHECK_TR_ALG
  SmartPtr<Journal> debug_jrnl = jnlst_->GetJournal("Debug");
  if (IsNull(debug_jrnl)) {
    debug_jrnl = jnlst_->AddFileJournal("Debug", "debug.out", J_ITERSUMMARY);
  }
  int debug_print_level = 0;
  debug_jrnl->SetAllPrintLevels((EJournalLevel)debug_print_level);
  debug_jrnl->SetPrintLevel(J_DBG, J_ALL);

  jnlst_->Printf(J_ALL, J_DBG, "\n");
  jnlst_->Printf(J_ALL, J_DBG, SINGLE_DIVIDER);
  jnlst_->Printf(J_ALL, J_DBG, "       The actual reduction is %23.16e\n",
                 actual_reduction_);
  jnlst_->Printf(J_ALL, J_DBG, "       The pred reduction is   %23.16e\n",
                 pred_reduction_);
  double ratio = actual_reduction_ / pred_reduction_;
  jnlst_->Printf(J_ALL, J_DBG, "       The calculated ratio is %23.16e\n",
                 ratio);
  jnlst_->Printf(J_ALL, J_DBG, "       The correct decision is ");
  if (ratio >= trust_region_ratio_accept_tol_)
    jnlst_->Printf(J_ALL, J_DBG, "to ACCEPT the trial point\n");
  else
    jnlst_->Printf(J_ALL, J_DBG, "to REJECT the trial point and change the "
                                 "trust-region radius\n");
  jnlst_->Printf(J_ALL, J_DBG, "       The TRUE decision is ");

  if (actual_reduction_ >= (trust_region_ratio_accept_tol_ * pred_reduction_)) {
    jnlst_->Printf(J_ALL, J_DBG, "to ACCEPT the trial point\n");
  } else
    jnlst_->Printf(J_ALL, J_DBG, "to REJECT the trial point and change the "
                                 "trust-region radius\n");
  jnlst_->Printf(J_ALL, J_DBG, SINGLE_DIVIDER);
  jnlst_->Printf(J_ALL, J_DBG, "\n");

#endif
#endif

#if 1
  if (actual_reduction_ >= (trust_region_ratio_accept_tol_ * pred_reduction_) &&
      actual_reduction_ >= -opt_tol_)
#else
  if (pred_reduction_ < -1.0e-8) {
    //    myQP_->WriteQPData(problem_name_+"qpdata.log");
    exitflag_ = PRED_REDUCTION_NEGATIVE;
    return;
  }
  if (actual_reduction_ >= (trust_region_ratio_accept_tol_ * pred_reduction_))
#endif
  {
    // succesfully update
    // copy information already calculated from the trial point
    infea_measure_ = infea_measure_trial_;

    obj_value_ = obj_value_trial_;
    x_k_->copy_vector(x_trial_);
    c_k_->copy_vector(c_trial_);
    // update function information by reading from sqp_nlp_ object
    get_multipliers();
    sqp_nlp_->eval_gradient(x_k_, grad_f_);
    sqp_nlp_->eval_jacobian(x_k_, jacobian_);
    sqp_nlp_->eval_hessian(x_k_, multiplier_cons_, hessian_);

    QPinfoFlag_.Update_A = true;
    QPinfoFlag_.Update_H = true;
    QPinfoFlag_.Update_bounds = true;
    QPinfoFlag_.Update_g = true;

    isaccept_ = true; // no need to calculate the SOC direction
  } else {
    isaccept_ = false;
  }
}

/**
 * @brief Update the trust region radius.
 *
 * This function update the trust-region radius when the ratio calculated by the
 * ratio test is smaller than eta_c or bigger than
 * trust_region_ratio_increase_tol and the
 * search_direction
 * hits the trust-region bounds.
 * If ratio<eta_c, the trust region radius will decrease by the parameter
 * gamma_c, to be gamma_c* delta_
 * If ratio_test> trust_region_ratio_increase_tol and delta_= norm_p_k_ the
 * trust-region radius will be
 * increased by the parameter gamma_c.
 *
 * If trust region radius has changed, the corresponding flags will be set to be
 * true;
 */

void SqpAlgorithm::update_radius()
{
  if (actual_reduction_ < trust_region_ratio_decrease_tol_ * pred_reduction_) {
    delta_ = trust_region_decrease_factor_ * delta_;
    QPinfoFlag_.Update_delta = true;
    // decrease the trust region radius. gamma_c is the parameter in options_
    // object
  } else {
    // printf("delta_ = %23.16e, ||p_k|| = %23.16e\n",delta_,p_k_->inf_norm());
    if (actual_reduction_ >
            trust_region_ratio_increase_tol_ * pred_reduction_ &&
        (opt_tol_ > fabs(delta_ - p_k_->calc_inf_norm()))) {
      delta_ =
          min(trust_region_increase_factor_ * delta_, trust_region_max_value_);
      QPinfoFlag_.Update_delta = true;
    }
  }

  // if the trust-region becomes too small, throw the error message

  if (delta_ < trust_region_min_value_) {

    SmartPtr<Journal> stdout_jrnl = jnlst_->GetJournal("console");
    if (IsValid(stdout_jrnl)) {
      // Set printlevel for stdout
      stdout_jrnl->SetAllPrintLevels((EJournalLevel)print_level_);
      stdout_jrnl->SetPrintLevel(J_DBG, J_NONE);
    }
    exit_flag_ = TRUST_REGION_TOO_SMALL;
    THROW_EXCEPTION(SMALL_TRUST_REGION, SMALL_TRUST_REGION_MSG);
  }
}

/**
 *
 * @brief This function checks how each constraint specified by the nlp readers
 * are
 * bounded.
 * If there is only upper bounds for a constraint, c_i(x)<=c^i_u, then
 * cons_type_[i]= BOUNDED_ABOVE
 * If there is only lower bounds for a constraint, c_i(x)>=c^i_l, then
 * cons_type_[i]= BOUNDED_BELOW
 * If there are both upper bounds and lower bounds, c^i_l<=c_i(x)<=c^i_u, and
 * c^i_l<c^i_u then cons_type_[i]= BOUNDED,
 * If there is no constraints on all
 * of c_i(x), then cons_type_[i]= UNBOUNDED;
 *
 * The same rules are also applied to the bound-constraints.
 */

static ConstraintType classify_single_constraint(double lower_bound,
                                                 double upper_bound)
{
  if (lower_bound > -INF && upper_bound < INF) {
    if ((upper_bound - lower_bound) < 1.0e-8) {
      return EQUAL;
    } else {
      return BOUNDED;
    }
  } else if (lower_bound > -INF && upper_bound > INF) {
    return BOUNDED_BELOW;
  } else if (upper_bound < INF && lower_bound < -INF) {
    return BOUNDED_ABOVE;
  } else {
    return UNBOUNDED;
  }
}

void SqpAlgorithm::classify_constraints_types()
{

  for (int i = 0; i < nCon_; i++) {
    cons_type_[i] =
        classify_single_constraint(c_l_->get_value(i), c_u_->get_value(i));
  }
  for (int i = 0; i < nVar_; i++) {
    bound_cons_type_[i] =
        classify_single_constraint(x_l_->get_value(i), x_u_->get_value(i));
  }
}

/**
 * @brief update the penalty parameter for the algorithm.
 *
 */
void SqpAlgorithm::update_penalty_parameter()
{

  infea_measure_model_ = myQP_->get_infea_measure_model();

  // prin/tf("infea_measure_model = %23.16e\n",infea_measure_model_);
  // printf("infea_measure_ = %23.16e\n",infea_measure_);
  if (infea_measure_model_ > penalty_update_tol_) {
    double infea_measure_model_tmp =
        infea_measure_model_; // temporarily store the value
    double rho_trial = rho_;  // the temporary trial value for rho
    setupLP();

    try {
      myLP_->solveLP(stats_);
    } catch (LP_NOT_OPTIMAL) {
      exit_flag_ = myLP_->get_status();
      THROW_EXCEPTION(LP_NOT_OPTIMAL, LP_NOT_OPTIMAL_MSG);
    }
    // calculate the infea_measure of the LP
    double infea_measure_infty = myLP_->get_infea_measure_model();

    //     printf("infea_measure_infty = %23.16e\n",infea_measure_infty);
    if (infea_measure_infty <= penalty_update_tol_) {
      // try to increase the penalty parameter to a number such that the
      // infeasibility measure of QP model with such penalty parameter
      // becomes zero

      while (infea_measure_model_ > penalty_update_tol_) {
        if (rho_trial >= penalty_parameter_max_value_) {
          break;
        } // TODO:safeguarded procedure...put here for now

        rho_trial =
            min(penalty_parameter_max_value_,
                rho_trial * penalty_parameter_increase_factor_); // increase rho

        stats_->penalty_change_trial_addone();

        myQP_->update_penalty(rho_trial);

        try {
          myQP_->solveQP(stats_, options_);
        } catch (QP_NOT_OPTIMAL) {
          exit_flag_ = myQP_->get_status();
          break;
        }

        // recalculate the infeasibility measure of the model by
        // calculating the one norm of the slack variables

        infea_measure_model_ = myQP_->get_infea_measure_model();
      }
    } else {
      while ((infea_measure_ - infea_measure_model_ <
                  eps1_ * (infea_measure_ - infea_measure_infty) &&
              (stats_->penalty_change_trial < penalty_iter_max_))) {

        if (rho_trial >= penalty_parameter_max_value_) {
          break;
        }

        // try to increase the penalty parameter to a number such that
        // the incurred reduction for the QP model is to a ratio to the
        // maximum possible reduction for current linear model.
        rho_trial = min(penalty_parameter_max_value_,
                        rho_trial * penalty_parameter_increase_factor_);

        stats_->penalty_change_trial_addone();

        myQP_->update_penalty(rho_trial);

        try {
          myQP_->solveQP(stats_, options_);
        } catch (QP_NOT_OPTIMAL) {
          exit_flag_ = myQP_->get_status();
          break;
        }

        // recalculate the infeasibility measure of the model by
        // calculating the one norm of the slack variables

        infea_measure_model_ = myQP_->get_infea_measure_model();
      }
    }
    // if any change occurs
    if (rho_trial > rho_) {
      if (rho_trial * infea_measure_ - get_obj_QP() >=
          eps2_ * rho_trial * (infea_measure_ - infea_measure_model_)) {
        //                    printf("rho_trial = %23.16e\n",rho_trial);
        //                    printf("infea_measure_ =
        //                    %23.16e\n",infea_measure_);
        stats_->penalty_change_Succ_addone();

        eps1_ += (1 - eps1_) * eps1_change_parm_;

        // use the new solution as the search direction
        get_search_direction();
        rho_ = rho_trial; // update to the class variable
        get_trial_point_info();
        qp_obj_ = get_obj_QP();
        double P1_x = obj_value_ + rho_ * infea_measure_;
        double P1_x_trial = obj_value_trial_ + rho_ * infea_measure_trial_;

        actual_reduction_ = P1_x - P1_x_trial;
        pred_reduction_ = rho_ * infea_measure_ - get_obj_QP();

      } else {
        //                    printf("rho_trial = %23.16e\n",rho_trial);
        //                    printf("infea_measure_ =
        //                    %23.16e\n",infea_measure_);
        stats_->penalty_change_Fail_addone();
        infea_measure_model_ = infea_measure_model_tmp;
        QPinfoFlag_.Update_penalty = true;
      }
    } else if (infea_measure_ < opt_tol_primal_feasibility_ * 1.0e-3) {
      //            rho_ = rho_*0.5;
      //            shared_ptr<Vector> sol_tmp = make_shared<Vector>(nVar_ + 2
      //            * nCon_);
      //            myQP_->update_penalty(rho_);
      //
      //            try {
      //                myQP_->solveQP(stats_, old_options_);
      //            }
      //            catch (QP_NOT_OPTIMAL) {
      //                handle_error("QP NOT OPTIMAL");
      //            }
      //
      //            //recalculate the infeasibility measure of the model by
      //            // calculating the one norm of the slack variables
      //
      //            infea_measure_model_ = myQP_->get_infea_measure_model();

      //            p_k_->copy_vector(sol_tmp);
      //            get_trial_point_info();
      //            qp_obj = get_obj_QP();
    }
  }
}

/**
 * @brief Use the Ipopt Reference Options and set it to default values.
 */
void SqpAlgorithm::register_options_(SmartPtr<RegisteredOptions> reg_options)
{

  reg_options->SetRegisteringCategory("trust-region");
  reg_options->AddBoundedNumberOption(
      "trust_region_ratio_decrease_tol",
      "trust-region parameter for the ratio test triggering decrease.", 0.,
      true, 1., true, 0.25,
      "If ratio <= trust_region_ratio_decrease_tol, then the trust-region "
      "radius for the next "
      "iteration will be decreased for the next iteration.");
  reg_options->AddBoundedNumberOption(
      "trust_region_ratio_accept_tol",
      "trust-region parameter for the ratio test.", 0., true, 1., true, 1.0e-8,
      "The trial point will be accepted if ratio >= "
      "trust_region_ratio_accept_tol. ");
  reg_options->AddBoundedNumberOption(
      "trust_region_ratio_increase_tol",
      "trust-region parameter for the ratio test.", 0., true, 1., true, 0.75,
      "If ratio >= trust_region_ratio_increase_tol and the search direction "
      "hits the  "
      "trust-region boundary, the trust-region radius will "
      "be increased for the next iteration.");
  reg_options->AddBoundedNumberOption(
      "trust_region_decrease_factor",
      "Factor used to reduce the trust-region size.", 0., true, 1., true, 0.5,
      "If the trust-region radius is going to be decreased, "
      "then it will be multiplied by the value of this options.");
  reg_options->AddLowerBoundedNumberOption(
      "trust_region_increase_factor",
      "Factor used to increase the trust-region size.", 1., true, 2.0,
      "If the trust-region radius is going to be "
      "increased, then it will be set as gamma_e*delta,"
      "where delta is current trust-region radius.");

  reg_options->AddLowerBoundedNumberOption("trust_region_init_value",
                                           "Initial trust-region radius value",
                                           0., true, 1.0);
  reg_options->AddLowerBoundedNumberOption(
      "trust_region_max_value", "Maximum value of trust-region radius "
                                "allowed for the radius update",
      0., true, 1e10);
  reg_options->AddLowerBoundedNumberOption(
      "trust_region_min_value", "Minimum value of trust-region radius "
                                "allowed for the radius update",
      0., true, 1e-16);

  reg_options->SetRegisteringCategory("Penalty Update");
  reg_options->AddLowerBoundedNumberOption(
      "penalty_parameter_init_value", "Initial value of the penalty parameter.",
      0., true, 1.0, "");
  reg_options->AddLowerBoundedNumberOption(
      "penalty_update_tol", "some tolerance.", 0., true, 1e-8, "");
  reg_options->AddLowerBoundedNumberOption(
      "penalty_parameter_increase_factor",
      "Factor by which penatly parameter is increased.", 1., true, 10., "");
  reg_options->AddNumberOption("eps1", "penalty update parameter something",
                               0.1, "");
  reg_options->AddNumberOption("eps1_change_parm",
                               "penalty update parameter something", 0.1, "");
  reg_options->AddNumberOption("eps2", "penalty update parameter something",
                               1.0e-6, "");
  reg_options->AddNumberOption("print_level_penalty_update",
                               "print level for penalty update", 0);
  reg_options->AddNumberOption("penalty_parameter_max_value",
                               "Maximum value of the penalty parameter", 1.0e8);
  reg_options->AddIntegerOption(
      "penalty_iter_max",
      "maximum number of penalty paramter update allowed in a "
      "single iteration in the main algorithm",
      200);
  reg_options->AddIntegerOption(
      "penalty_iter_max_total",
      "maximum number of penalty paramter update allowed "
      "in total",
      100);

  reg_options->SetRegisteringCategory("Optimality Test");
  reg_options->AddIntegerOption("testOption_NLP",
                                "Level of Optimality test for "
                                "NLP",
                                0);
  reg_options->AddStringOption2(
      "auto_gen_tol", "Tell the algorithm to automatically"
                      "generate the tolerance level for optimality test "
                      "based on information from NLP",
      "no", "no", "will use user-defined values of tolerance for"
                  " the optimality test",
      "yes", "will automatically generate the tolerance "
             "level for the optimality test");
  reg_options->AddNumberOption("active_set_tol", "",
                               1.0e-5); // TODO: make lower bounded options
  reg_options->AddNumberOption("opt_tol", "", 1.0e-8);
  reg_options->AddNumberOption("opt_tol_complementarity", "", 1.0e-4);
  reg_options->AddNumberOption("opt_tol_dual_feasibility", " ", 1.0e-4);
  reg_options->AddNumberOption("opt_tol_primal_feasibility", " ", 1.0e-4);
  reg_options->AddNumberOption("opt_tol_stationarity_feasibility", "", 1e-4);
  reg_options->AddNumberOption("opt_second_tol", " ", 1.0e-8);

  reg_options->AddLowerBoundedNumberOption(
      "cpu_time_limit", "CPU time limit", 0., true, 1e10,
      "Time limit measured in CPU time (in seconds)");
  reg_options->AddLowerBoundedNumberOption(
      "wallclock_time_limit", "Wallclock time limit", 0., true, 1e10,
      "Time limit measured in wallclock time (in seconds)");

  reg_options->SetRegisteringCategory("General");
  reg_options->AddNumberOption("step_size_tol",
                               "the smallest stepsize can be accepted"
                               "before concluding convergence",
                               1.0e-15);
  reg_options->AddIntegerOption("max_num_iterations",
                                "Maximum number of iteration for the algorithm",
                                3000);
  reg_options->AddStringOption2(
      "perform_second_order_correction_step",
      "Tells the algorithm to calculate the second-order correction step "
      "during the main iteration"
      "yes",
      "no", "not calculate the soc steps", "yes",
      "will calculate the soc steps", "");

  reg_options->SetRegisteringCategory("QPsolver");
  reg_options->AddIntegerOption("testOption_QP",
                                "Level of Optimality test for QP", -99);
  reg_options->AddIntegerOption("qp_solver_max_num_iterations",
                                "maximum number of iteration for the "
                                "QP solver in solving each QP",
                                1000);
  reg_options->AddIntegerOption("lp_solver_max_num_iterations",
                                "maximum number of iteration for the "
                                "LP solver in solving each LP",
                                1000);
  reg_options->AddIntegerOption("qp_solver_print_level",
                                "print level for QP solver", 0);
  reg_options->AddStringOption4(
      "qp_solver_choice", "QP solver used for step computation.", "", "QORE",
      "QPOASE", "", "QORE", "", "GUROBI", "", "CPLEX", "");

  //    reg_options->AddStringOption("QPsolverChoice",
  //		    "The choice of QP solver which will be used in the
  // Algorithm",
  //		    "qpOASES");

  reg_options->SetRegisteringCategory("LPsolver");
  //    reg_options->AddStringOption("LPsolverChoice",
  //		    "The choice of LP solver which will be used in the
  // Algorithm",
  //		    "qpOASES");

  reg_options->AddIntegerOption("testOption_LP",
                                "Level of Optimality test for LP", -99);
  reg_options->AddNumberOption("iter_max_lp",
                               "maximum number of iteration for the "
                               "LP solver in solving each LP",
                               100);
  reg_options->AddNumberOption("print_level_lp", "print level for LP solver",
                               0);
}

void SqpAlgorithm::get_option_values_()
{
  print_level_ = 2; // This should be taken care of before?

  options_->GetIntegerValue("max_num_iterations", max_num_iterations_, "");
  options_->GetNumericValue("cpu_time_limit", cpu_time_limit_, "");
  options_->GetNumericValue("wallclock_time_limit", wallclock_time_limit_, "");

  options_->GetNumericValue("trust_region_init_value", trust_region_init_value_,
                            "");
  options_->GetNumericValue("trust_region_max_value", trust_region_max_value_,
                            "");
  options_->GetNumericValue("trust_region_min_value", trust_region_min_value_,
                            "");
  options_->GetNumericValue("trust_region_ratio_decrease_tol",
                            trust_region_ratio_decrease_tol_, "");
  options_->GetNumericValue("trust_region_ratio_accept_tol",
                            trust_region_ratio_accept_tol_, "");
  options_->GetNumericValue("trust_region_ratio_increase_tol",
                            trust_region_ratio_increase_tol_, "");
  options_->GetNumericValue("trust_region_decrease_factor",
                            trust_region_decrease_factor_, "");
  options_->GetNumericValue("trust_region_increase_factor",
                            trust_region_increase_factor_, "");

  options_->GetNumericValue("penalty_parameter_init_value",
                            penalty_parameter_init_value_, "");
  options_->GetNumericValue("penalty_update_tol", penalty_update_tol_, "");
  options_->GetNumericValue("penalty_parameter_increase_factor",
                            penalty_parameter_increase_factor_, "");
  options_->GetNumericValue("penalty_parameter_max_value",
                            penalty_parameter_max_value_, "");
  options_->GetNumericValue("eps1", eps1_, "");
  options_->GetNumericValue("eps1_change_parm", eps1_change_parm_, "");
  options_->GetNumericValue("eps2", eps2_, "");
  options_->GetIntegerValue("penalty_iter_max", penalty_iter_max_, "");

  options_->GetBoolValue("perform_second_order_correction_step",
                         perform_second_order_correction_step_, "");

  options_->GetNumericValue("active_set_tol", active_set_tol_, "");
  options_->GetNumericValue("opt_tol", opt_tol_, "");
  options_->GetNumericValue("opt_tol_primal_feasibility",
                            opt_tol_primal_feasibility_, "");
  options_->GetNumericValue("opt_tol_dual_feasibility",
                            opt_tol_dual_feasibility_, "");
  options_->GetNumericValue("opt_tol_stationarity_feasibility",
                            opt_tol_stationarity_feasibility_, "");
  options_->GetNumericValue("opt_tol_complementarity", opt_tol_complementarity_,
                            "");

  /** QP solver usde for ??? */
  int enum_int;
  options_->GetEnumValue("qp_solver_choice", enum_int, "");
  qp_solver_choice_ = Solver(enum_int);
}

void SqpAlgorithm::second_order_correction()
{
  // FIXME: check correctness
  if ((!isaccept_) && perform_second_order_correction_step_) {
    isaccept_ = false;

#ifdef DEBUG
//#ifdef CHECK_SOC
//        EJournalLevel debug_print_level = old_options_->debug_print_level;
//        SmartPtr<Journal> debug_jrnl = jnlst_->GetJournal("Debug");
//        if (IsNull(debug_jrnl)) {
//            debug_jrnl = jnlst_->AddFileJournal("Debug", "debug.out",
//            J_ITERSUMMARY);
//        }
//        debug_jrnl->SetAllPrintLevels(debug_print_level);
//        debug_jrnl->SetPrintLevel(J_DBG, J_ALL);
//
//
//        cout << "       Entering the SOC STEP calculation\n";
//        cout << "       class member isaccept is ";
//        if (isaccept_)
//            cout << "TRUE" << endl;
//        else
//            cout << "FALSE" << endl;
//        cout << "---------------------------------------------------------\n";
//
//        cout << endl;
//#endif
#endif

    shared_ptr<Vector> p_k_tmp =
        make_shared<Vector>(nVar_); // for temporarily storing data for p_k
    p_k_tmp->copy_vector(p_k_);

    shared_ptr<Vector> s_k =
        make_shared<Vector>(nVar_); // for storing solution for SOC
    shared_ptr<Vector> tmp_sol = make_shared<Vector>(nVar_ + 2 * nCon_);

    double norm_p_k_tmp = norm_p_k_;
    double qp_obj_tmp = qp_obj_;
    shared_ptr<Vector> Hp = make_shared<Vector>(nVar_);
    hessian_->multiply(p_k_, Hp); // Hp = H_k*p_k
    Hp->add_vector(1., grad_f_);  //(H_k*p_k+g_k)
    myQP_->update_grad(Hp);
    myQP_->update_bounds(delta_, x_l_, x_u_, x_trial_, c_l_, c_u_, c_trial_);
    norm_p_k_ = p_k_->calc_inf_norm();

    try {
      myQP_->solveQP(stats_, options_);
    } catch (QP_NOT_OPTIMAL) {
      myQP_->WriteQPData(problem_name_ + "qpdata.log");
      exit_flag_ = myQP_->get_status();
      THROW_EXCEPTION(QP_NOT_OPTIMAL, QP_NOT_OPTIMAL_MSG);
    }
    tmp_sol->copy_vector(myQP_->get_optimal_solution());
    s_k->copy_vector(tmp_sol);

    qp_obj_ = get_obj_QP() + (qp_obj_tmp - rho_ * infea_measure_model_);
    p_k_->add_vector(1., s_k);
    get_trial_point_info();
    ratio_test();
    if (!isaccept_) {
      p_k_->copy_vector(p_k_tmp);
      qp_obj_ = qp_obj_tmp;
      norm_p_k_ = norm_p_k_tmp;
      myQP_->update_grad(grad_f_);
      myQP_->update_bounds(delta_, x_l_, x_u_, x_k_, c_l_, c_u_, c_k_);
    }
  }
}

/**
*@brief get the objective value of QP from myQP object
*@relates QPhandler.hpp
*@return QP obejctive
*/

double SqpAlgorithm::get_obj_QP()
{
  return (myQP_->get_objective());
}

////////////////////////////////////////////////////////////////////////////

void SqpAlgorithm::print_final_stats()
{
  jnlst_->Printf(J_ITERSUMMARY, J_MAIN, "======================================"
                                        "===================================="
                                        "============================\n");

  // Determine string describing the exit status
  string exit_status;
  switch (exit_flag_) {
    case OPTIMAL:
      exit_status = "Optimal solution found.";
      break;
    case PRED_REDUCTION_NEGATIVE:
      exit_status = "Error: Predict reduction is negative.";
      break;
    case INVALID_NLP:
      exit_status = "Error: Invalid NLP.";
      break;
    case EXCEED_MAX_ITERATIONS:
      exit_status = "Maximum number of iterations exceeded.";
      break;
    case EXCEED_MAX_CPU_TIME:
      exit_status = "CPU time limit exceeded.";
      break;
    case EXCEED_MAX_WALLCLOCK_TIME: // TODO NEXT
      exit_status = "Wallclock time limit exceeded.";
      break;
    case TRUST_REGION_TOO_SMALL:
      exit_status = "Trust region becomes too small.";
      break;
    case QPERROR_INFEASIBLE:
      exit_status = "Error: QP solver claims that QP is infeasible.";
      break;
    case QPERROR_UNBOUNDED:
      exit_status = "Error: QP solver claims that QP is unbounded.";
      break;
    case QPERROR_EXCEED_MAX_ITER:
      exit_status = "Error: QP solver exceeded internal iteration limit.";
      break;
    case QPERROR_UNKNOWN:
      exit_status = "Error: Unknown QP solver error.";
      break;
#if 0
    case QP_OPTIMAL:
    case QPERROR_NOTINITIALISED:
    case CONVERGE_TO_NONOPTIMAL:
    case QPERROR_PREPARINGAUXILIARYQP:
    case QPERROR_AUXILIARYQPSOLVED:
    case QPERROR_PERFORMINGHOMOTOPY:
    case QPERROR_HOMOTOPYQPSOLVED:
      exit_status = "Should not appear as exit code.";
      break;
#endif
    default:
      exit_status =
          "Error: exit_flag has uncaught value " + to_string(exit_flag_) + ".";
      break;
  }

  // Print the exit status
  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "Exit status:                                                %23s\n",
      exit_status.c_str());

  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "Number of Variables                                         %23i\n",
      nVar_);
  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "Number of Constraints                                       %23i\n",
      nCon_);
  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "Iterations:                                                 %23i\n",
      stats_->iter);
  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "QP Solver Iterations:                                       %23i\n",
      stats_->qp_iter);
  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "Final Objectives:                                           %23.16e\n",
      obj_value_);
  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "Primal Feasibility Violation                                %23.16e\n",
      opt_status_.primal_violation);

  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "Dual Feasibility Violation                                  %23.16e\n",
      opt_status_.dual_violation);
  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "Complmentarity Violation                                    %23.16e\n",
      opt_status_.compl_violation);
  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "Stationarity Violation                                      %23.16e\n",
      opt_status_.stationarity_violation);
  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "||p_k||                                                     %23.16e\n",
      norm_p_k_);
  jnlst_->Printf(
      J_ITERSUMMARY, J_MAIN,
      "||c_k||                                                     %23.16e\n",
      infea_measure_);

  jnlst_->Printf(J_ITERSUMMARY, J_MAIN, DOUBLE_LONG_DIVIDER);
}

} // END_NAMESPACE_SQPHOTSTART