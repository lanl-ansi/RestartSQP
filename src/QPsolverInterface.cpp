#include "QPsolverInterface.hpp"

namespace SQPhotstart {

    /**Default constructor*/
    qpOASESInterface::qpOASESInterface() {
    }

    /**
     * @name Constructor which also initializes the qpOASES SQProblem objects
     * @param nVar_QP the number of variables in QP problem
     * @param nCon_QP the number of constraints in QP problem (the number of rows of A)
     */
    qpOASESInterface::qpOASESInterface(const int nVar_QP, const int nCon_QP) {
        //FIXME: the qpOASES does not accept any extra input
        _qp = std::make_shared<qpOASES::SQProblem>((qpOASES::int_t) nVar_QP, (qpOASES::int_t) nCon_QP);

    }

    /**Default destructor*/
    qpOASESInterface::~qpOASESInterface() {
        delete[] H_tmp_.RowInd_;
        H_tmp_.RowInd_ = NULL;
        delete[] H_tmp_.ColInd_;
        H_tmp_.ColInd_ = NULL;
        delete[] H_tmp_.MatVal_;
        H_tmp_.MatVal_ = NULL;
        delete[] A_tmp_.RowInd_;
        A_tmp_.RowInd_ = NULL;
        delete[] A_tmp_.ColInd_;
        A_tmp_.ColInd_ = NULL;
        delete[] A_tmp_.MatVal_;
        A_tmp_.MatVal_ = NULL;
    }

/**
 * @name This function solves the QP problem specified in the data, with given options. After the QP
 * is solved, it updates the stats, adding the iteration number used to solve the QP to the qp_iter
 * in object stats
 */
    bool qpOASESInterface::optimizeQP(shared_ptr<Matrix> H, shared_ptr<Vector> g, shared_ptr<Matrix> A,
                                      shared_ptr<Vector> lbA, shared_ptr<Vector> ubA, shared_ptr<Vector> lb,
                                      shared_ptr<Vector> ub, shared_ptr<Stats> stats, shared_ptr<Options> options) {
//        qpOASESMatrixAdapter(H, H_);
//        qpOASESMatrixAdapter(A, A_);
//        H_->createDiagInfo();
//
//        qpOASES::int_t nWSR = options->qp_maxiter;//TODO modify it
//        if (stats->qp_iter == 0) {//if haven't solve any QP before then initialize the first qp
//            qpOASES::Options qp_options;
//            if (options->qpPrintLevel == 0)//else use the default print level in qpOASES
//                qp_options.printLevel = qpOASES::PL_NONE;
//            _qp->setOptions(qp_options);
//            _qp->init(H_.get(), g->vector(), A_.get(), lb->vector(), ub->vector(), lbA->vector(), ubA->vector(), nWSR,
//                      0);
//        } else
//            _qp->hotstart(H_.get(), g->vector(), A_.get(), lb->vector(), ub->vector(), lbA->vector(), ubA->vector(),
//                          nWSR, 0);
//        stats->qp_iter_addValue((int) nWSR);
        return true;
    }

/**
 * This function transforms the representation form of the sparse matrix in triplet form
 * to adapt the format required by the qpOASES(Harwell-Boeing Sparse Matrix).
 *
 * @param M_in_triplet Matrix objects contains data in triplet form
 * @param M_result     Matrix object prepared as the input for qpOASES
 */
    bool qpOASESInterface::qpOASESMatrixAdapter(shared_ptr<Matrix> M_in_triplet,
                                                shared_ptr<qpOASES::SparseMatrix> M_result) {

//        if (!A_tmp_.isinitialized) {
//            std::vector<std::tuple<int, int, Number, int>> tmp;
//            for (int i = 0; i < M_in_triplet->EntryNum(); i++) {
//                tmp.push_back(
//                        make_tuple(M_in_triplet->RowIndex_at(i), M_in_triplet->ColIndex_at(i),
//                                   M_in_triplet->MatVal_at(i),
//                                   M_in_triplet->order_at(i)));
//            }
//            for (int i = 0; i < M_in_triplet->num_I(); i++)
//                for (int j = 0; j < M_in_triplet->size_I_at(i); j++)
//                    tmp.push_back(make_tuple(M_in_triplet->RowIndex_I_at(i) + j, M_in_triplet->ColIndex_I_at(i) + j,
//                                             M_in_triplet->sign_I_at(i), tmp.size()));
//            std::sort(tmp.begin(), tmp.end(), tuple_sort_rule);
//            //copy the order information back
//            for (int i = 0; i < M_in_triplet->EntryNum(); i++) {
//                M_in_triplet->get_order(i, get<3>(tmp[i]));
//            }
//            delete_repetitive_entry(tmp);
//            for(int i  = 0; i<tmp.size(); i++){
//                cout<<get<0>(tmp[i])<<"    ";
//                cout<<get<1>(tmp[i])<<"    ";
//                cout<<get<2>(tmp[i])<<"    ";
//                cout<<get<3>(tmp[i])<<endl;
//            }
//            initialize_qpOASES_input(tmp, M_result, M_in_triplet->RowNum(), M_in_triplet->ColNum(), true);
//            A_tmp_.isinitialized = true;
//            tmp.clear();
//        } else {
//            update_qpOASES_input(M_in_triplet, M_result);
//        }
        return true;
    }

    bool qpOASESInterface::qpOASESMatrixAdapter(shared_ptr<Matrix> M_in_triplet,
                                                shared_ptr<qpOASES::SymSparseMat> M_result) {
//        if (!H_tmp_.isinitialized) {
//            std::vector<std::tuple<int, int, Number, int>> tmp;
//            for (int i = 0; i < M_in_triplet->EntryNum(); i++) {
//                tmp.push_back(
//                        make_tuple(M_in_triplet->RowIndex_at(i), M_in_triplet->ColIndex_at(i),
//                                   M_in_triplet->MatVal_at(i),
//                                   M_in_triplet->order_at(i)));
//            }
//            std::sort(tmp.begin(), tmp.end(), tuple_sort_rule);
//            //copy the order information back
//            for (int i = 0; i < M_in_triplet->EntryNum(); i++) {
//                M_in_triplet->get_order(i, get<3>(tmp[i]));
//            }
//
//            delete_repetitive_entry(tmp);
//            for(int i  = 0; i<tmp.size(); i++){
//                cout<<get<0>(tmp[i])<<"    ";
//                cout<<get<1>(tmp[i])<<"    ";
//                cout<<get<2>(tmp[i])<<"    ";
//                cout<<get<3>(tmp[i])<<endl;
//            }
//            initialize_qpOASES_input(tmp, M_result, M_in_triplet->RowNum(), M_in_triplet->ColNum(), false);
//            H_tmp_.isinitialized = true;
//            tmp.clear();
//        } else {
//            update_qpOASES_input(M_in_triplet, M_result);
//        }

        return true;
    }

/**
 * This is part of qpOASESMatrixAdapter
 *@name process the input data. Transform sorted data in tuple form to the format required by qpOASES QProblem class.
 * It will initialize the matrix object required by the qpOASES
 *
 * @tparam T either be SparseMatrix or SymSparseMat
 * @param input The input data in tuple form, it is the form used to store data in Matrix class
 * @param results The Matrix to be initialize
 * @param RowNum The number of rows of the matrix
 * @param ColNum The number of columns of the matrix
 */
    template<typename T>
    bool qpOASESInterface::initialize_qpOASES_input(vector<tuple<int, int, Number, int>> input,
                                                    shared_ptr<T> results, Index RowNum, Index ColNum,
                                                    bool isA) {
        if (isA) {
//            A_tmp_.RowInd_ = new qpOASES::sparse_int_t[(int) input.size()];
//            A_tmp_.ColInd_ = new qpOASES::sparse_int_t[ColNum + 1];
//            A_tmp_.MatVal_ = new qpOASES::real_t[(int) input.size()];
//
//            int j = get<1>(input[0]);
//            for (int i = 0; i < (int) input.size(); i++) {
//                A_tmp_.RowInd_[i] = (qpOASES::sparse_int_t) get<0>(input[i]) - 1;
//                A_tmp_.ColInd_[i] = (qpOASES::real_t) get<2>(input[i]);
//                if (get<1>(input[i]) == j)A_tmp_.ColInd_[j]++;
//                else {
//                    A_tmp_.ColInd_[get<1>(input[i])] = (qpOASES::sparse_int_t) A_tmp_.ColInd_[get<1>(input[i - 1])] + 1;
//                    j = get<1>(input[i]);
//                }
//
//            }
//            for (int i = get<1>(input[input.size() - 1]); i <= ColNum; i++) {
//                A_tmp_.ColInd_[i] = A_tmp_.ColInd_[get<1>(input[input.size() - 1])];
//            }
//            for(int i =0; i<input.size();i++) cout<<A_tmp_.RowInd_[i]<<"   ";
//            cout<<" "<<endl;
//            for(int i =0; i<=ColNum;i++) cout<<A_tmp_.ColInd_[i]<<"   ";
//            cout<<" "<<endl;
//            for(int i =0; i<input.size();i++) cout<<A_tmp_.MatVal_at(i)<<"   ";
//            cout<<" "<<endl;
//            results = std::make_shared<T>(RowNum, ColNum, A_tmp_.RowInd_, A_tmp_.ColInd_, A_tmp_.MatVal_);
//        } else {
//            H_tmp_.RowInd_ = new qpOASES::sparse_int_t[(int) input.size()]();
//            H_tmp_.ColInd_ = new qpOASES::sparse_int_t[ColNum + 1]();
//            H_tmp_.MatVal_ = new qpOASES::real_t[(int) input.size()]();
//
//            int j = get<1>(input[0]);
//            for (int i = 0; i < (int) input.size(); i++) {
//                H_tmp_.RowInd_[i] = (qpOASES::sparse_int_t) get<0>(input[i]) - 1;
//                H_tmp_.MatVal_[i] = (qpOASES::real_t) get<2>(input[i]);
//                if (get<1>(input[i]) == j)H_tmp_.ColInd_[j]++;
//                else {
//                    H_tmp_.ColInd_[get<1>(input[i])] = (qpOASES::sparse_int_t) H_tmp_.ColInd_[get<1>(input[i - 1])] + 1;
//                    j = get<1>(input[i]);
//                }
//
////                for(int k =0; k<=ColNum;k++) cout<<H_tmp_.ColInd_[k];
////                cout<<"  "<<endl;
//            }
//            for (int i = get<1>(input[input.size() - 1]); i <= ColNum; i++) {
//                H_tmp_.ColInd_[i] = H_tmp_.ColInd_[get<1>(input[input.size() - 1])];
//            }
////            for(int i =0; i<input.size();i++) cout<<H_tmp_.RowInd_[i]<<endl;
////            for(int i =0; i<=ColNum;i++) cout<<H_tmp_.ColInd_[i]<<endl;
////            for(int i =0; i<input.size();i++) cout<<H_tmp_.MatVal_at(i)<<endl;
//            results = std::make_shared<T>(RowNum, ColNum, H_tmp_.RowInd_, H_tmp_.ColInd_, H_tmp_.MatVal_);

        }
        return true;
    }


}//SQPHOTSTART

