/* Copyright (C) 2019
 * All Rights Reserved.
 *
 * Authors: Xinyi Luo
 * Date:2019-06
 */
#include <sqphot/Algorithm.hpp>


namespace SQPhotstart {

DECLARE_STD_EXCEPTION(NEW_POINTS_WITH_INCREASE_OBJ_ACCEPTED);

DECLARE_STD_EXCEPTION(SMALL_TRUST_REGION);

/**
 * Default Constructor
 */
Algorithm::Algorithm() :
    Active_Set_constraints_(NULL),
    Active_Set_bounds_(NULL),
    cons_type_(NULL),
    bound_cons_type_(NULL),
    norm_p_k_(0),
    obj_value_(0),
    obj_value_trial_(0),
    pred_reduction_(0),
    qp_obj_(0),
    rho_(0),
    actual_reduction_(0),
    delta_(0),
    infea_measure_(0),
    infea_measure_model_(0) {
    jnlst_ = new Ipopt::Journalist();
#if DEBUG
    jnrl_level_= Ipopt::J_INSUPPRESSIBLE;
#else
    jnrl_level_ = Ipopt::J_SUMMARY;
#endif
    roptions2_ = new Ipopt::OptionsList();
}


/**
 * Default Destructor
 */
Algorithm::~Algorithm() {

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
void Algorithm::Optimize() {
    while (stats_->iter < options_->iter_max && exitflag_ == UNKNOWN) {
        setupQP();
        try {
            myQP_->solveQP(stats_,
                           options_);//solve the QP subproblem and update the stats_
        }
        catch (QP_NOT_OPTIMAL) {
            handle_error("QP NOT OPTIMAL");
            break;
        }


        //get the search direction from the solution of the QPsubproblem
        get_search_direction();

        get_obj_QP();

        //Update the penalty parameter if necessary

        update_penalty_parameter();

        //calculate the infinity norm of the search direction
        norm_p_k_ = p_k_->getInfNorm();

        get_trial_point_info();

        ratio_test();

        // Calculate the second-order-correction steps
        second_order_correction();

        // Update the radius and the QP bounds if the radius has been changed
        stats_->iter_addone();
        /* output some information to the console*/

        //check if the current iterates is optimal and decide to
        //exit the loop or not
        if (options_->printLevel >= 2) {
            if (stats_->iter % 10 == 0) {
                jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_HEADER);
                jnlst_->Printf(J_ITERSUMMARY, J_MAIN, DOUBLE_LONG_DIVIDER);
            }
            jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_OUTPUT);
        }
        termination_check();
        if (exitflag_ != UNKNOWN) {
            break;
        }

        try {
            update_radius();
        }
        catch (SMALL_TRUST_REGION) {
            break;
        }

    }

    //check if the current iterates get_status before exiting
    if (stats_->iter == options_->iter_max)
        exitflag_ = EXCEED_MAX_ITER;

    if (exitflag_ != OPTIMAL && exitflag_ != INVALID_NLP) {
        termination_check();
    }

    // print the final summary message to the console
    if (options_->printLevel > 0)
        print_final_statsitics();
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
void Algorithm::termination_check() {
    //FIXME: not sure if it is better to use the new multiplier or the old one

    int i;
    get_multipliers();
    /**-------------------------------------------------------**/
    /**               Check the KKT conditions                **/
    /**-------------------------------------------------------**/

    /**-------------------------------------------------------**/
    /**                    Identify Active Set                **/
    /**-------------------------------------------------------**/

    if (Active_Set_constraints_ == NULL)

        Active_Set_constraints_ = new ActiveType[nCon_];
    if (Active_Set_bounds_ == NULL)
        Active_Set_bounds_ = new ActiveType[nVar_];

    for (i = 0; i < nCon_; i++) {
        if (cons_type_[i] == BOUNDED_ABOVE) {
            if (abs(c_u_->values()[i] - c_k_->values()[i]) <
                    options_->active_set_tol)
                Active_Set_constraints_[i] = ACTIVE_ABOVE;
        } else if (cons_type_[i] == BOUNDED_BELOW) {
            if (abs(c_k_->values()[i] - c_l_->values()[i]) <
                    options_->active_set_tol) {
                Active_Set_constraints_[i] = ACTIVE_BELOW;
            }
        } else if (cons_type_[i] == EQUAL) {
            if ((abs(c_u_->values()[i] - c_k_->values()[i]) <
                    options_->active_set_tol) &&
                    (abs(c_k_->values()[i] - c_l_->values()[i]) <
                     options_->active_set_tol))
                Active_Set_constraints_[i] = ACTIVE_BOTH_SIDE;
        } else {
            Active_Set_constraints_[i] = INACTIVE;
        }
    }


    for (i = 0; i < nVar_; i++) {
        if (bound_cons_type_[i] == BOUNDED_ABOVE) {
            if (abs(x_u_->values()[i] - x_k_->values()[i]) <
                    options_->active_set_tol)
                Active_Set_bounds_[i] = ACTIVE_ABOVE;
        } else if (bound_cons_type_[i] == BOUNDED_BELOW) {
            if (abs(x_k_->values()[i] - x_l_->values()[i]) <
                    options_->active_set_tol)
                Active_Set_bounds_[i] = ACTIVE_BELOW;
        } else if (bound_cons_type_[i] == EQUAL) {
            if ((abs(x_u_->values()[i] - x_k_->values()[i]) <
                    options_->active_set_tol) &&
                    (abs(x_k_->values()[i] - x_l_->values()[i]) <
                     options_->active_set_tol))
                Active_Set_bounds_[i] = ACTIVE_BOTH_SIDE;
        } else {
            Active_Set_bounds_[i] = INACTIVE;
        }
    }
    /**-------------------------------------------------------**/
    /**                    Primal Feasibility                 **/
    /**-------------------------------------------------------**/


    if (infea_measure_ < options_->opt_prim_fea_tol) {
        opt_status_.primal_feasibility = true;
    } else
        opt_status_.primal_feasibility = false;

    /**-------------------------------------------------------**/
    /**                    Dual Feasibility                   **/
    /**-------------------------------------------------------**/

    i = 0;
    opt_status_.dual_feasibility = true;
    while (i < nVar_ && opt_status_.dual_feasibility) {
        if (bound_cons_type_[i] == BOUNDED_ABOVE &&
                multiplier_vars_->values()[i] > options_->opt_dual_fea_tol) {
            opt_status_.dual_feasibility = false;
        } else if (bound_cons_type_[i] == BOUNDED_BELOW &&
                   multiplier_vars_->values()[i] < -options_->opt_dual_fea_tol) {
            opt_status_.dual_feasibility = false;
        }
        i++;
    }

    i = 0;
    while (i < nCon_ && opt_status_.dual_feasibility) {
        if (cons_type_[i] == BOUNDED_ABOVE &&
                multiplier_cons_->values()[i] > options_->opt_dual_fea_tol) {
            opt_status_.dual_feasibility = false;
        } else if (cons_type_[i] == BOUNDED_BELOW &&
                   multiplier_cons_->values()[i] < -options_->opt_dual_fea_tol) {
            opt_status_.dual_feasibility = false;
        }
        i++;
    }

    /**-------------------------------------------------------**/
    /**                    Complemtarity                      **/
    /**-------------------------------------------------------**/


    i = 0;
    opt_status_.complementarity = true;
    while (i < nCon_ && opt_status_.complementarity) {
        if (cons_type_[i] == BOUNDED_ABOVE) {
            if (abs(multiplier_cons_->values()[i] *
                    (c_u_->values()[i] - c_k_->values()[i]))
                    > options_->opt_compl_tol) {
                opt_status_.complementarity = false;
            }
        } else if (cons_type_[i] == BOUNDED_BELOW) {
            if (abs(multiplier_cons_->values()[i] *
                    (c_k_->values()[i] - c_l_->values()[i]))
                    > options_->opt_compl_tol) {
                opt_status_.complementarity = false;
            }
        } else if (cons_type_[i] == UNBOUNDED) {
            if (multiplier_cons_->values()[i] > options_->opt_compl_tol) {
                opt_status_.complementarity = false;
            }
        }
        i++;
    }

    i = 0;
    while (i < nVar_ && opt_status_.complementarity) {
        if (bound_cons_type_[i] == BOUNDED_ABOVE) {
            if (abs(multiplier_vars_->values()[i] *
                    (x_u_->values()[i] - x_k_->values()[i]))
                    > options_->opt_compl_tol) {
                opt_status_.complementarity = false;
            }

        } else if (bound_cons_type_[i] == BOUNDED_BELOW) {
            if (abs(multiplier_vars_->values()[i] *
                    (x_k_->values()[i] - x_l_->values()[i]))
                    > options_->opt_compl_tol) {
                opt_status_.complementarity = false;
            }
        } else if (bound_cons_type_[i] == UNBOUNDED) {
            if (multiplier_vars_->values()[i] > options_->opt_compl_tol) {
                opt_status_.complementarity = false;
            }
        }
        i++;
    }


    /**-------------------------------------------------------**/
    /**                    Stationarity                       **/
    /**-------------------------------------------------------**/

    shared_ptr<Vector> difference = make_shared<Vector>(nVar_);
    // the difference of g-J^T y -\lambda
    jacobian_->transposed_times(multiplier_cons_, difference);
    difference->add_vector(multiplier_vars_->values());
    difference->subtract_vector(grad_f_->values());


    if (difference->getInfNorm() > options_->opt_tol) {
        opt_status_.stationarity = false;
    } else {
        opt_status_.stationarity = true;
    }


    /**-------------------------------------------------------**/
    /**             Decide if x_k is optimal                  **/
    /**-------------------------------------------------------**/


    if (opt_status_.primal_feasibility && opt_status_.dual_feasibility &&
            opt_status_.complementarity && opt_status_.stationarity) {
        opt_status_.first_order_opt = true;
        exitflag_ = OPTIMAL;
    } else {
        if (norm_p_k_ > delta_ + options_->tol) {
            exitflag_ = STEP_LARGER_THAN_TRUST_REGION;
        }
        //if it is not optimal
#if DEBUG
        EJournalLevel debug_print_level = options_->debug_print_level;
        SmartPtr<Journal> debug_jrnl = jnlst_->GetJournal("Debug");
        if (IsNull(debug_jrnl)) {
            debug_jrnl = jnlst_->AddFileJournal("Debug", "debug.out", J_ITERSUMMARY);
        }
        debug_jrnl->SetAllPrintLevels(debug_print_level);
        debug_jrnl->SetPrintLevel(J_DBG, J_ALL);
        if (CHECK_TERMINATION) {
            jnlst_->Printf(J_DETAILED, J_DBG,
                           DOUBLE_DIVIDER);
            jnlst_->Printf(J_DETAILED, J_DBG, "           Iteration  %i\n", stats_->iter);
            jnlst_->Printf(J_DETAILED, J_DBG, DOUBLE_DIVIDER);
            grad_f_->print("grad_f", jnlst_, J_DBG, J_WARNING);
            jnlst_->Printf(J_DETAILED, J_DBG, SINGLE_DIVIDER);
            c_u_->print("c_u", jnlst_, J_DBG, J_WARNING);
            c_l_->print("c_l", jnlst_, J_DBG, J_WARNING);
            c_k_->print("c_k", jnlst_, J_DBG, J_WARNING);
            multiplier_cons_->print("multiplier_cons", jnlst_, J_DBG, J_WARNING);
            jnlst_->Printf(J_DETAILED, J_DBG, SINGLE_DIVIDER);
            x_u_->print("x_u", jnlst_, J_DBG, J_WARNING);
            x_l_->print("x_l", jnlst_, J_DBG, J_WARNING);
            x_k_->print("x_k", jnlst_, J_DBG, J_WARNING);
            multiplier_vars_->print("multiplier_vars", jnlst_, J_DBG, J_WARNING);
            jnlst_->Printf(J_DETAILED, J_DBG, SINGLE_DIVIDER);
            jacobian_->print_full("jacobian", jnlst_);
            hessian_->print_full("hessian", jnlst_);
            difference->print("stationarity gap", jnlst_,J_DBG,J_DETAILED);
            jnlst_->Printf(J_DETAILED, J_DBG, SINGLE_DIVIDER);
            jnlst_->Printf(J_DETAILED, J_DBG, "Feasibility      ");
            jnlst_->Printf(J_DETAILED, J_DBG, "%i\n",
                           opt_status_.primal_feasibility);
            jnlst_->Printf(J_DETAILED, J_DBG, "Dual Feasibility ");
            jnlst_->Printf(J_DETAILED, J_DBG, "%i\n", opt_status_.dual_feasibility);
            jnlst_->Printf(J_DETAILED, J_DBG, "Stationarity     ");
            jnlst_->Printf(J_DETAILED, J_DBG, "%i\n", opt_status_.stationarity);
            jnlst_->Printf(J_DETAILED, J_DBG, "Complementarity  ");
            jnlst_->Printf(J_DETAILED, J_DBG, "%i\n", opt_status_.complementarity);
            jnlst_->Printf(J_DETAILED, J_DBG, SINGLE_DIVIDER);
        }
#endif

    }
}


void Algorithm::get_trial_point_info() {

    x_trial_->copy_vector(x_k_->values());//make a deep copy
    x_trial_->add_vector(p_k_->values());

    // Calculate f_trial, c_trial and infea_measure_trial for the trial points
    // x_trial
    nlp_->Eval_f(x_trial_, obj_value_trial_);
    nlp_->Eval_constraints(x_trial_, c_trial_);

    cal_infea_trial();
}


/**
 *  @brief This function initializes the objects required by the SQP Algorithm,
 *  copies some parameters required by the algorithm, obtains the function
 *  information for the first QP.
 *
 */
void Algorithm::initialization(SmartPtr<Ipopt::TNLP> nlp) {

    allocate_memory(nlp);

    /*-----------------------------------------------------*/
    /*         Get the nlp information                     */
    /*-----------------------------------------------------*/
    nlp_->Get_bounds_info(x_l_, x_u_, c_l_, c_u_);
    nlp_->Get_starting_point(x_k_, multiplier_cons_);

    //shift starting point to satisfy the bound constraint
    nlp_->shift_starting_point(x_k_, x_l_, x_u_);
    nlp_->Eval_f(x_k_, obj_value_);
    nlp_->Eval_gradient(x_k_, grad_f_);
    nlp_->Eval_constraints(x_k_, c_k_);
    nlp_->Get_Structure_Hessian(x_k_, multiplier_cons_, hessian_);
    nlp_->Eval_Hessian(x_k_, multiplier_cons_, hessian_);
    nlp_->Get_Strucutre_Jacobian(x_k_, jacobian_);
    nlp_->Eval_Jacobian(x_k_, jacobian_);
    classify_constraints_types();

    cal_infea(); //calculate the infeasibility measure for x_k

    /*-----------------------------------------------------*/
    /*                      JOURNAL INIT                   */
    /*-----------------------------------------------------*/


    SmartPtr<Journal> stdout_jrnl =
        jnlst_->AddFileJournal("console", "stdout", J_ITERSUMMARY);
    stdout_jrnl->SetPrintLevel(J_DBG, J_NONE);
#if DEBUG
    SmartPtr<Journal> debug_jrnl = jnlst_->AddFileJournal("Debug", "debug.out", J_ITERSUMMARY);

    debug_jrnl->SetPrintLevel(J_DBG, J_ALL);
    auto tr_jrnl =jnlst_->AddFileJournal("trust-region","trust_region.out",
                                         J_DETAILED);
    tr_jrnl->SetPrintLevel(J_DBG,J_DETAILED);
#endif

    /*-----------------------------------------------------*/
    /*                      OUTPUT                         */
    /*-----------------------------------------------------*/
    if (options_->printLevel > 1) {
        SmartPtr<Journal> stdout_jrnl = jnlst_->GetJournal("console");
        if (IsValid(stdout_jrnl)) {
            // Set printlevel for stdout
            stdout_jrnl->SetAllPrintLevels(options_->print_level);
            stdout_jrnl->SetPrintLevel(J_DBG, J_NONE);
        }
        jnlst_->Printf(J_ITERSUMMARY, J_MAIN, DOUBLE_LONG_DIVIDER);
        jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_HEADER);
        jnlst_->Printf(J_ITERSUMMARY, J_MAIN, DOUBLE_LONG_DIVIDER);
        jnlst_->Printf(J_ITERSUMMARY, J_MAIN, STANDARD_OUTPUT);
    }

}


/**
 * @brief alloocate memory for class members.
 * This function initializes all the shared pointer which will be used in the
 * Algorithm::Optimize, and it copies all parameters that might be changed during
 * the run of the function Algorithm::Optimize.
 *
 * @param nlp: the nlp reader that read data of the function to be minimized;
 */
void Algorithm::allocate_memory(SmartPtr<Ipopt::TNLP> nlp) {

    nlp_ = make_shared<SQPTNLP>(nlp);

    nVar_ = nlp_->nlp_info_.nVar;
    nCon_ = nlp_->nlp_info_.nCon;

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

    jacobian_ = make_shared<SpTripletMat>(nlp_->nlp_info_.nnz_jac_g, nCon_, nVar_,
                                          false);
    hessian_ = make_shared<SpTripletMat>(nlp_->nlp_info_.nnz_h_lag, nVar_, nVar_,
                                         true);
    //TODO: use roptions instead of this one
    options_ = make_shared<Options>();
    stats_ = make_shared<Stats>();
    log_ = make_shared<Log>();

    myQP_ = make_shared<QPhandler>(nlp_->nlp_info_, options_->QPsolverChoice);
    myLP_ = make_shared<LPhandler>(nlp_->nlp_info_);

    delta_ = options_->delta;
    rho_ = options_->rho;
}


/**
 * @brief This function calculates the infeasibility measure for either trial point
 * or current iterate x_k
 *
 *@param trial: true if the user are going to evaluate the infeasibility measure of
 * the trial point _x_trial;
 *	infea_measure_trial = norm(-max(c_trial-cu,0),1)+norm(-min(c_trial-cl,0),1)
 *
 * 	            false if the user are going to evaluate the infeasibility measure
 * 	            of the current iterates _x_k
 *	infea_measure = norm(-max(c-cu,0),1)+norm(-min(c-cl,0),1);
 *
 */
void Algorithm::cal_infea_trial() {

    infea_measure_trial_ = 0;
    for (int i = 0; i < c_k_->Dim(); i++) {
        if (c_trial_->values()[i] < c_l_->values()[i])
            infea_measure_trial_ += (c_l_->values()[i] - c_trial_->values()[i]);
        else if (c_trial_->values()[i] > c_u_->values()[i])
            infea_measure_trial_ += (c_trial_->values()[i] - c_u_->values()[i]);
    }

}


void Algorithm::cal_infea() {

    infea_measure_ = 0;
    for (int i = 0; i < c_k_->Dim(); i++) {
        if (c_k_->values()[i] < c_l_->values()[i])
            infea_measure_ += (c_l_->values()[i] - c_k_->values()[i]);
        else if (c_k_->values()[i] > c_u_->values()[i])
            infea_measure_ += (c_k_->values()[i] - c_u_->values()[i]);
    }
}


/**
 * @brief This function extracts the search direction for NLP from the QP subproblem
 * solved, and copies it to the class member _p_k
 *
 * It will truncate the optimal solution of QP into two parts, the first half (with
 * length equal to the number of variables) to be the search direction.
 *
 * @param qphandler the QPhandler class object used for solving a QP subproblem with
 * specified QP information
 */
void Algorithm::get_search_direction() {
    p_k_->copy_vector(myQP_->GetOptimalSolution());
//    p_k_->print("p_k");

    if (options_->penalty_update)
        infea_measure_model_ = oneNorm(myQP_->GetOptimalSolution() + nVar_,
                                       2 * nCon_);
    //FIXME:calculate somewhere else?
}


/**
 * @brief This function extracts the the Lagragian multipliers for constraints
 * in NLP and copies it to the class member lambda_
 *
 *   Note that the QP subproblem will return a multiplier for the constraints
 *   and the bound in a single vector, so we only take the first #constraints
 *   number of elements as an approximation of multipliers for the nlp problem
 *
 * @param qphandler the QPsolver class object used for solving a QP subproblem
 * with specified QP information
 */

void Algorithm::get_multipliers() {
//    if (multiplier_cons_->isAllocated())
//        multiplier_cons_->free();
    if (options_->QPsolverChoice == QORE_QP) {

        multiplier_cons_->copy_vector(myQP_->GetMultipliers() + nVar_ + 2 * nCon_);
        multiplier_vars_->copy_vector(myQP_->GetMultipliers());
    } else if (options_->QPsolverChoice == QPOASES_QP) {
        multiplier_cons_->copy_vector(myQP_->GetMultipliers() + 2 * nCon_ + nVar_);
        multiplier_vars_->copy_vector(myQP_->GetMultipliers());
    }

}


/**
 * @brief This function will set up the data for the QP subproblem
 *
 * It will initialize all the data at once at the beginning of the Algorithm. After
 * that, the data in the QP problem will be updated according to the class
 * member QPinfoFlag_
 */

DECLARE_STD_EXCEPTION(QP_UNCHANGED);


void Algorithm::setupQP() {
    if (stats_->iter == 0) {
        myQP_->set_bounds(delta_, x_k_, x_l_, x_u_, c_k_, c_l_, c_u_);
        myQP_->set_g(grad_f_, rho_);
        myQP_->set_A(jacobian_);
        myQP_->set_H(hessian_);
    } else {
        if ((!QPinfoFlag_.Update_g) && (!QPinfoFlag_.Update_H) &&
                (!QPinfoFlag_.Update_A)
                && (!QPinfoFlag_.Update_bounds) && (!QPinfoFlag_.Update_delta) &&
                (!QPinfoFlag_.Update_penalty))
            THROW_EXCEPTION(QP_UNCHANGED, "QP is not changed");

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


void Algorithm::setupLP() {

    myLP_->set_bounds(delta_, x_k_, x_l_, x_u_, c_k_, c_l_, c_u_);
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
 * q_k(p; rho) = f_k+ g_k^Tp +1/2 p^T H_k p+rho* infeasibility_measure_model is the
 * quadratic model at x_k.
 * The trial point  will be accepted if the ratio >= eta_s.
 * If it is accepted, the function will also updates the gradient, Jacobian
 * information by reading from nlp_ object. The corresponding flags of class member
 * QPinfoFlag_ will set to be true.
 */
void Algorithm::ratio_test() {

    using namespace std;


    double P1x = obj_value_ + rho_ * infea_measure_;
    double P1_x_trial = obj_value_trial_ + rho_ * infea_measure_trial_;

    actual_reduction_ = P1x - P1_x_trial;
    pred_reduction_ = rho_ * infea_measure_ - qp_obj_;


#if DEBUG
#if CHECK_TR_ALG
    SmartPtr<Journal> debug_jrnl = jnlst_->GetJournal("Debug");
    if (IsNull(debug_jrnl)) {
        debug_jrnl = jnlst_->AddFileJournal("Debug", "debug.out", J_ITERSUMMARY);
    }
    debug_jrnl->SetAllPrintLevels(debug_print_level);
    debug_jrnl->SetPrintLevel(J_DBG, J_ALL);



    cout << endl;
    cout << "---------------------------------------------------------\n";
    cout << "       The actual reduction is " << actual_reduction_ << endl;
    cout << "       The pred reduction is" << pred_reduction_ << endl;
    double ratio = actual_reduction_ / pred_reduction_;
    cout << "       The calculated ratio is" << ratio << endl;
    cout << "       The correct decision is ";

    if (ratio >= options_->eta_s)
        cout << "to ACCEPT the trial point\n";
    else
        cout
                << "to REJECT the trial point and change the truest-region radius\n";
    cout << "       The TRUE decision is ";

    if (actual_reduction_ >= (options_->eta_s * pred_reduction_)) {
        cout << "to ACCEPT the trial point\n";
    }
    else
        cout
                << "to REJECT the trial point and change the truest-region radius\n";

    cout << "---------------------------------------------------------\n";
    cout << endl;

#endif
#endif

#if 1
    if (actual_reduction_ >= (options_->eta_s * pred_reduction_)
            && actual_reduction_ >= -options_->tol) {
#else
    assert(pred_reduction_>0);
    if (actual_reduction_ >= (options_->eta_s * pred_reduction_) ) {
#endif

        //succesfully update
        //copy information already calculated from the trial point
        infea_measure_ = infea_measure_trial_;

        obj_value_ = obj_value_trial_;
        x_k_->copy_vector(x_trial_->values());
        c_k_->copy_vector(c_trial_->values());
        //update function information by reading from nlp_ object
        get_multipliers();
        nlp_->Eval_gradient(x_k_, grad_f_);
        nlp_->Eval_Jacobian(x_k_, jacobian_);
        nlp_->Eval_Hessian(x_k_, multiplier_cons_, hessian_);

        QPinfoFlag_.Update_A = true;
        QPinfoFlag_.Update_H = true;
        QPinfoFlag_.Update_bounds = true;
        QPinfoFlag_.Update_g = true;

        isaccept_ = true;    //no need to calculate the SOC direction
    } else {
        isaccept_ = false;
    }

}

/**
 * @brief Update the trust region radius.
 *
 * This function update the trust-region radius when the ratio calculated by the
 * ratio test is smaller than eta_c or bigger than eta_e and the search_direction
 * hits the trust-region bounds.
 * If ratio<eta_c, the trust region radius will decrease by the parameter
 * gamma_c, to be gamma_c* delta_
 * If ratio_test> eta_e and delta_= norm_p_k_ the trust-region radius will be
 * increased by the parameter gamma_c.
 *
 * If trust region radius has changed, the corresponding flags will be set to be
 * true;
 */


void Algorithm::update_radius() {

    if (actual_reduction_ < options_->eta_c * pred_reduction_) {
        delta_ = options_->gamma_c * delta_;
        QPinfoFlag_.Update_delta = true;
        //decrease the trust region radius. gamma_c is the parameter in options_ object
    } else {
        if (actual_reduction_ > options_->
                eta_e * pred_reduction_
                && options_->tol > (delta_ - norm_p_k_)) {
            delta_ = std::min(options_->gamma_e * delta_, options_->delta_max);
            QPinfoFlag_.Update_delta = true;
        }
    }

    //if the trust-region becomes too small, throw the error message

    if (delta_ < options_->delta_min) {
        exitflag_ = TRUST_REGION_TOO_SMALL;
//                    jnlst_->Printf(J_WARNING,jnrl_category_,
//                            "Trust-region is too small!");
        THROW_EXCEPTION(SMALL_TRUST_REGION, "The trust region is smaller than"
                        "the user-defined minimum value");
    }
}


/**
 *
 * @brief This function checks how each constraint specified by the nlp readers are
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


void Algorithm::classify_constraints_types() {

    for (int i = 0; i < nCon_; i++) {
        cons_type_[i] = classify_single_constraint(c_l_->values()[i],
                        c_u_->values()[i]);
    }
    for (int i = 0; i < nVar_; i++) {
        bound_cons_type_[i] = classify_single_constraint(x_l_->values()[i],
                              x_u_->values()[i]);
    }
}


/**
 * @brief update the penalty parameter for the algorithm.
 *
 */
void Algorithm::update_penalty_parameter() {

    if (options_->penalty_update) {
        if (infea_measure_model_ > options_->penalty_update_tol) {

            double infea_measure_model_tmp = infea_measure_model_;//temporarily store the value
            double rho_trial = rho_;//the temporary trial value for rho

            setupLP();

            try {
                myLP_->solveLP(stats_, options_);
            }
            catch (LP_NOT_OPTIMAL) {
                handle_error("LP NOT OPTIMAL");
            }

            shared_ptr<Vector> sol_tmp = make_shared<Vector>(nVar_ + 2 * nCon_);
            get_full_direction_LP(sol_tmp);

            //calculate the infea_measure of the LP
            double infea_measure_infty = oneNorm(sol_tmp->values() + nVar_,
                                                 2 * nCon_);

            log_->print_penalty_update(stats_->penalty_change_trial, rho_trial,
                                       infea_measure_model_, infea_measure_infty);

            if (infea_measure_infty <= options_->penalty_update_tol) {
                //try to increase the penalty parameter to a number such that the
                // infeasibility measure of QP model with such penalty parameter
                // becomes zero

                while (infea_measure_model_ > options_->penalty_update_tol) {
                    if (rho_trial >= options_->rho_max) {
                        break;
                    }//TODO:safeguarded procedure...put here for now

                    rho_trial *= std::min(options_->rho_max,
                                          options_->increase_parm);  //increase rho

                    stats_->penalty_change_trial_addone();

                    myQP_->update_penalty(rho_trial);

                    try {
                        myQP_->solveQP(stats_, options_);
                    }
                    catch (QP_NOT_OPTIMAL) {
                        handle_error("QP NOT OPTIMAL");
                    }

                    //recalculate the infeasibility measure of the model by
                    // calculating the one norm of the slack variables
                    get_full_direction_QP(sol_tmp);

                    infea_measure_model_ = oneNorm(sol_tmp->values() + nVar_,
                                                   2 * nCon_);

                    log_->print_penalty_update(stats_->penalty_change_trial,
                                               rho_trial, infea_measure_model_,
                                               infea_measure_infty);
                }

            } else {
                while ((infea_measure_ - infea_measure_model_ <
                        options_->eps1 * (infea_measure_ - infea_measure_infty) &&
                        (stats_->penalty_change_trial <
                         options_->penalty_iter_max))) {

                    if (rho_trial * 2 >= options_->rho_max) {
                        break;
                    }

                    //try to increase the penalty parameter to a number such that
                    // the incurred reduction for the QP model is to a ratio to the
                    // maximum possible reduction for current linear model.
                    rho_trial *= std::min(options_->rho_max, options_->increase_parm);

                    stats_->penalty_change_trial_addone();

                    myQP_->update_penalty(rho_trial);

                    try {
                        myQP_->solveQP(stats_, options_);
                    }
                    catch (QP_NOT_OPTIMAL) {
                        handle_error("QP NOT OPTIMAL");
                    }

                    //recalculate the infeasibility measure of the model by
                    // calculating the one norm of the slack variables
                    get_full_direction_QP(sol_tmp);

                    infea_measure_model_ = oneNorm(sol_tmp->values() + nVar_,
                                                   2 * nCon_);

                    log_->print_penalty_update(stats_->penalty_change_trial,
                                               rho_trial, infea_measure_model_,
                                               infea_measure_infty);
                }
            }
            //if any change occurs
            if (rho_trial > rho_) {
                if (rho_trial * infea_measure_ - qp_obj_ >=
                        options_->eps2 * rho_trial *
                        (infea_measure_ - infea_measure_model_)) {
                    stats_->penalty_change_Succ_addone();

                    options_->eps1 +=
                        (1 - options_->eps1) * options_->eps1_change_parm;

                    //use the new solution as the search direction
                    p_k_->copy_vector(sol_tmp);

                    rho_ = rho_trial; //update to the class variable

                    qp_obj_ = myQP_->GetObjective();//update the qp_obj
                } else {

                    stats_->penalty_change_Fail_addone();
                    infea_measure_model_ = infea_measure_model_tmp;
                    QPinfoFlag_.Update_penalty = true;
                }
            }
        }
    }
}


/**
 * @brief Use the Ipopt Reference Options and set it to default values.
 */
void Algorithm::setDefaultOption() {

    roptions = new Ipopt::RegisteredOptions();
    roptions->SetRegisteringCategory("trust-region");
    roptions->AddNumberOption("eta_c", "trust-region parameter for the ratio test.",
                              0.25,
                              "If ratio<=eta_c, then the trust-region radius for the next "
                              "iteration will be decreased for the next iteration.");
    roptions->AddNumberOption("eta_s", "trust-region parameter for the ratio test.",
                              1.0e-8,
                              "The trial point will be accepted if ratio>= eta_s. ");
    roptions->AddNumberOption("eta_e", "trust-region parameter for the ratio test.",
                              0.75,
                              "If ratio>=eta_e and the search direction hits the  "
                              "trust-region boundary, the trust-region radius will "
                              " be increased for the next iteration.");
    roptions->AddNumberOption("gamma_c", "radius update parameter",
                              0.5,
                              "If the trust-region radius is going to be decreased,"
                              " then it will be set as gamma_c*delta, where delta "
                              "is current trust-region radius.");
    roptions->AddNumberOption("gamma_e", "radius update parameter",
                              2.0,
                              "If the trust-region radius is going to be "
                              "increased, then it will be set as gamma_e*delta,"
                              "where delta is current trust-region radius.");
    roptions->AddNumberOption("delta_0", "initial trust-region radius value", 1.0);
    roptions->AddNumberOption("delta_max", "the maximum value of trust-region radius"
                              " allowed for the radius update", 1.0e8);

    roptions->SetRegisteringCategory("Penalty Update");
    roptions->AddNumberOption("eps1", "penalty update parameter", 0.3, "");
    roptions->AddNumberOption("eps2", "penalty update parameter", 1.0e-6, "");
    roptions->AddNumberOption("print_level_penalty_update",
                              "print level for penalty update", 0);
    roptions->AddNumberOption("rho_max", "maximum value of penalty parameter", 1.0e6);
    roptions->AddNumberOption("increase_parm",
                              "the number which will be use for scaling the new "
                              "penalty parameter", 10);
    roptions->AddIntegerOption("penalty_iter_max",
                               "maximum number of penalty paramter update allowed in a "
                               "single iteration in the main algorithm", 10);
    roptions->AddIntegerOption("penalty_iter_max_total",
                               "maximum number of penalty paramter update allowed "
                               "in total", 100);

    roptions->SetRegisteringCategory("Optimality Test");
    roptions->AddIntegerOption("testOption_NLP", "Level of Optimality test for "
                               "NLP", 0);
    roptions->AddStringOption2("auto_gen_tol",
                               "Tell the algorithm to automatically"
                               "generate the tolerance level for optimality test "
                               "based on information from NLP",
                               "no",
                               "no", "will use user-defined values of tolerance for"
                               " the optimality test",
                               "yes", "will automatically generate the tolerance "
                               "level for the optimality test");
    roptions->AddNumberOption("opt_tol", "", 1.0e-5);
    roptions->AddNumberOption("active_set_tol", "", 1.0e-5);
    roptions->AddNumberOption("opt_compl_tol", "", 1.0e-6);
    roptions->AddNumberOption("opt_dual_fea_tol", " ", 1.0e-6);
    roptions->AddNumberOption("opt_prim_fea_tol", " ", 1.0e-5);
    roptions->AddNumberOption("opt_second_tol", " ", 1.0e-8);

    roptions->SetRegisteringCategory("General");
    roptions->AddNumberOption("step_size_tol", "the smallest stepsize can be accepted"
                              "before concluding convergence",
                              1.0e-15);
    roptions->AddNumberOption("iter_max",
                              "maximum number of iteration for the algorithm", 10);
    roptions->AddNumberOption("print_level", "print level for main algorithm", 2);
    roptions->AddStringOption2(
        "second_order_correction",
        "Tells the algorithm to calculate the second-order correction step "
        "during the main iteration"
        "yes",
        "no", "not calculate the soc steps",
        "yes", "will calculate the soc steps",
        "");

    roptions->SetRegisteringCategory("QPsolver");
    roptions->AddIntegerOption("testOption_QP",
                               "Level of Optimality test for QP", -99);
    roptions->AddNumberOption("iter_max_qp", "maximum number of iteration for the "
                              "QP solver in solving each QP", 100);
    roptions->AddNumberOption("print_level_qp", "print level for QP solver", 0);

    //    roptions->AddStringOption("QPsolverChoice",
    //		    "The choice of QP solver which will be used in the Algorithm",
    //		    "qpOASES");


    roptions->SetRegisteringCategory("LPsolver");
    //    roptions->AddStringOption("LPsolverChoice",
    //		    "The choice of LP solver which will be used in the Algorithm",
    //		    "qpOASES");

    roptions->AddIntegerOption("testOption_LP",
                               "Level of Optimality test for LP", -99);
    roptions->AddNumberOption("iter_max_lp", "maximum number of iteration for the "
                              "LP solver in solving each LP", 100);
    roptions->AddNumberOption("print_level_lp", "print level for LP solver", 0);

}


void
Algorithm::get_full_direction_QP(shared_ptr<SQPhotstart::Vector> search_direction) {
    //Can also swp the pointer...
    search_direction->copy_vector(myQP_->GetOptimalSolution());
}


void Algorithm::get_full_direction_LP(shared_ptr<Vector> search_direction) {
    search_direction->copy_vector(myLP_->GetOptimalSolution());
}


void Algorithm::second_order_correction() {
    //FIXME: check correctness
    if ((!isaccept_) && options_->second_order_correction) {
        isaccept_ = false;

#if DEGUG
#if CHECK_SOC
        cout << endl;
        cout << "---------------------------------------------------------\n";
        cout << "       Entering the SOC STEP calculation\n";
        cout << "       class member isaccept is ";
        if (isaccept_)
            std::cout << "TRUE" << endl;
        else
            std::cout << "FALSE" << endl;
        cout << "---------------------------------------------------------\n";

        cout << endl;
#endif
#endif

        shared_ptr<Vector> p_k_tmp = make_shared<Vector>(nVar_); //for temporarily
        shared_ptr<Vector> s_k = make_shared<Vector>(nVar_);
        shared_ptr<Vector> tmp_sol = make_shared<Vector>(nVar_ + 2 * nCon_);
        p_k_tmp->copy_vector(p_k_);

        double norm_p_k_tmp = norm_p_k_;
        double qp_obj_tmp = qp_obj_;
        shared_ptr<Vector> Htimesp = make_shared<Vector>(nVar_);
        hessian_->times(p_k_, Htimesp);
        Htimesp->add_vector(grad_f_->values());
        myQP_->update_grad(Htimesp);
        myQP_->update_bounds(delta_, x_l_, x_u_, x_trial_, c_l_, c_u_, c_trial_);
        norm_p_k_ = p_k_->getInfNorm();

        try {
            myQP_->solveQP(stats_, options_);
        }
        catch (QP_NOT_OPTIMAL) {
            handle_error("QP NOT OPTIMAL");
        }
//TODO rewrite
//        myQP_->GetOptimalSolution();
        s_k->copy_vector(tmp_sol->values());

        qp_obj_ = myQP_->GetObjective() + (qp_obj_tmp - rho_ * infea_measure_model_);
        p_k_->add_vector(s_k->values());
        get_trial_point_info();
        ratio_test();
        if (!isaccept_)
            p_k_ = p_k_tmp;
        qp_obj_ = qp_obj_tmp;
        norm_p_k_ = norm_p_k_tmp;

    }

}


void Algorithm::handle_error(const char* error) {

    if (error != nullptr) {
        if (strcmp(error, "QP NOT OPTIMAL") == 0 ||
                strcmp(error, "LP NOT OPTIMAL") == 0) {
            switch (myQP_->GetStatus()) {
            case QP_INFEASIBLE:
                exitflag_ = QPERROR_INFEASIBLE;
                break;
            case QP_UNBOUNDED:
                exitflag_ = QPERROR_UNBOUNDED;
                break;
            case QP_NOTINITIALISED:
                exitflag_ = QPERROR_NOTINITIALISED;
                break;
            case QP_HOMOTOPYQPSOLVED:
                exitflag_ = QPERROR_HOMOTOPYQPSOLVED;
                break;
            case QP_PERFORMINGHOMOTOPY:
                exitflag_ = QPERROR_PERFORMINGHOMOTOPY;
                break;
            case QP_AUXILIARYQPSOLVED:
                exitflag_ = QPERROR_AUXILIARYQPSOLVED;
                break;
            case QP_PREPARINGAUXILIARYQP:
                exitflag_ = QPERROR_PREPARINGAUXILIARYQP;
                break;
            }
        } else if (strcmp(error, "INVALID NLP") == 0) {
            exitflag_ = INVALID_NLP;
        }
    }
}

void Algorithm::get_obj_QP() {

    if (options_->QPsolverChoice == QPOASES_QP)
        qp_obj_ = myQP_->GetObjective();
    else if (options_->QPsolverChoice == QORE_QP) {
        shared_ptr<Vector> Hp = make_shared<Vector>(nVar_);
        hessian_->times(p_k_, Hp);//H*p_k
        qp_obj_ = 0.5 * p_k_->times(Hp) + p_k_->times(grad_f_) +
                  infea_measure_model_ * rho_;
    }
}


void Algorithm::print_final_statsitics() {

    SmartPtr<Journal> stdout_jrnl = jnlst_->GetJournal("console");
    if (IsValid(stdout_jrnl)) {
        // Set printlevel for stdout
        stdout_jrnl->SetAllPrintLevels(options_->print_level);
        stdout_jrnl->SetPrintLevel(J_DBG, J_NONE);
    }


    jnlst_->Printf(J_SUMMARY,J_MAIN,DOUBLE_LONG_DIVIDER);
    switch (exitflag_) {
    case OPTIMAL:
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "OPTIMAL");
        break;
    case INVALID_NLP :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "INVALID_NLP");
        break;
    case EXCEED_MAX_ITER :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "EXCEED_MAX_ITER");
        break;
    case QPERROR_INTERNAL_ERROR :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "QP_INTERNAL_ERROR");
        break;
    case QPERROR_INFEASIBLE :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "QP_INFEASIBLE");
        break;
    case QPERROR_UNBOUNDED :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "QP_UNBOUNDED");
        break;
    case QPERROR_EXCEED_MAX_ITER :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "QP_EXCEED_MAX_ITER");
        break;
    case QPERROR_NOTINITIALISED :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "QP_NOTINITIALISED");
        break;
    case AUXINPUT_NOT_OPTIMAL :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "AUXINPUT_NOT_OPTIMAL");
        break;
    case CONVERGE_TO_NONOPTIMAL :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "CONVERGE_TO_NONOPTIMAL");
        break;

    case QPERROR_PREPARINGAUXILIARYQP:

        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "QPERROR_PREPARINGAUXILIARYQP");
        break;
    case QPERROR_AUXILIARYQPSOLVED:
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "QPERROR_AUXILIARYQPSOLVED");
        break;
    case QPERROR_PERFORMINGHOMOTOPY  :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "QPERROR_PERFORMINGHOMOTOPY");
        break;
    case QPERROR_HOMOTOPYQPSOLVED    :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "QPERROR_HOMOTOPYQPSOLVED");
        break;
    case TRUST_REGION_TOO_SMALL:
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "TRUST_REGION_TOO_SMALL");
        break;

    case STEP_LARGER_THAN_TRUST_REGION:
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "STEP_LARGER_THAN_TRUST_REGION");
        break;
    case UNKNOWN :
        jnlst_->Printf(J_SUMMARY,J_MAIN,"Exitflag:                                                   %23s\n",
                       "UNKNOWN ERROR");

        break;

    }
    jnlst_->Printf(J_SUMMARY,J_MAIN,"Number of Variables                                         %23i\n",
                   nVar_);
    jnlst_->Printf(J_SUMMARY,J_MAIN,"Number of Constraints                                       %23i\n",
                   nCon_);
    jnlst_->Printf(J_SUMMARY,J_MAIN,"Iterations:                                                 %23i\n",
                   stats_->iter);
    jnlst_->Printf(J_SUMMARY,J_MAIN,"QP Solver Iterations:                                       %23i\n",
                   stats_->qp_iter);
    jnlst_->Printf(J_SUMMARY,J_MAIN,"Final Objectives:                                           %23e\n",
                   obj_value_);
    jnlst_->Printf(J_SUMMARY,J_MAIN,"||p_k||                                                     %23e\n",
                   norm_p_k_);
    jnlst_->Printf(J_SUMMARY,J_MAIN,"||c_k||                                                     %23e\n",
                   infea_measure_);

    jnlst_->Printf(J_SUMMARY,J_MAIN,DOUBLE_LONG_DIVIDER);

}

}//END_NAMESPACE_SQPHOTSTART


