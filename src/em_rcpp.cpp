#define EIGEN_PERMANENTLY_DISABLE_STUPID_WARNINGS
// #define EIGEN_USE_MKL_ALL
#include <Rcpp.h>
#include <RcppEigen.h>
// #include <Eigen/Sparse>
// #include <unsupported/Eigen/SparseExtra>
// #include <Eigen/PardisoSupport>

using namespace Rcpp;
using namespace Eigen;

//' Find the log of the determinant of Q_tilde
//'
//' @param kappa2 a scalar
//' @param in_list a list with elements Cmat, Gmat, and GtCinvG
//' @param n_sess the integer number of sessions
//' @export
// [[Rcpp::export(rng = false)]]
double logDetQt(double kappa2, const Rcpp::List &in_list, double n_sess) {
  // Load parameters
  Eigen::SparseMatrix<double> Cmat     = Eigen::SparseMatrix<double> (in_list["Cmat"]);
  Eigen::SparseMatrix<double> Gmat     = Eigen::SparseMatrix<double> (in_list["Gmat"]);
  Eigen::SparseMatrix<double> GtCinvG     = Eigen::SparseMatrix<double> (in_list["GtCinvG"]);
  // Create SparseMatrix Q
  Eigen::SparseMatrix<double> Q= kappa2 * Cmat + 2.0 * Gmat + GtCinvG / kappa2;
  SimplicialLDLT<Eigen::SparseMatrix<double>> cholQ(Q);
  double lDQ = n_sess * cholQ.vectorD().array().log().sum();
  return lDQ;
}

// Pulled off of Stack Overflow, sorts an n x 3 matrix by the first column
void eigen_sort_rows_by_head(Eigen::MatrixXd& A_nx3)
{
  std::vector<Eigen::VectorXd> vec;
  for (int64_t i = 0; i < A_nx3.rows(); ++i)
    vec.push_back(A_nx3.row(i));

  std::sort(vec.begin(), vec.end(), [](Eigen::VectorXd const& t1, Eigen::VectorXd const& t2){ return t1(0) < t2(0); } );

  for (int64_t i = 0; i < A_nx3.rows(); ++i)
    A_nx3.row(i) = vec[i];
};

void makeQt(Eigen::SparseMatrix<double>* Q, double kappa2, const Rcpp::List &spde) {
  Eigen::SparseMatrix<double> Cmat     = Eigen::SparseMatrix<double> (spde["Cmat"]);
  Eigen::SparseMatrix<double> Gmat     = Eigen::SparseMatrix<double> (spde["Gmat"]);
  Eigen::SparseMatrix<double> GtCinvG     = Eigen::SparseMatrix<double> (spde["GtCinvG"]);
  Eigen::SparseMatrix<double> Qt = kappa2 * Cmat + 2. * Gmat + GtCinvG / kappa2;
  for (int k=0; k<Qt.outerSize(); ++k) {
    for (SparseMatrix<double,0,int>::InnerIterator it(Qt,k); it; ++it) {
      Q->coeffRef(it.row(), it.col()) = it.value();
    }
  }
  // return Qt;
}

Eigen::MatrixXd makeV(int n_spde, int Ns) {
  Eigen::MatrixXd V(n_spde,Ns);
  Rcpp::NumericVector x(1);
  for(int i = 0; i < n_spde; i++) {
    for(int j = 0; j < Ns; j++) {
      // x = std::rand() / ((RAND_MAX + 1u) / 2);
      x = Rcpp::runif(1);
      if(x[0] <= 0.5) {
        V(i,j) = -1;
      } else {
        V(i,j) = 1;
      }
    }
  }
  return V;
}

//' Second derivative of the first term in the objective function for kappa2
//'
//' @param spde a list with elements Cmat, Gmat, and GtCinvG
//' @param grid_size the number of grid points at which to numerically evaluate
//'   the second derivative.
//' @param Ns the integer number of samples for the Hutchinson approximation
//' @param grid_limit the largest number in the grid. The grid starts at -2.0.
//' @param n_sess The number of runs in the data
//' @export
// [[Rcpp::export(rng = false)]]
Eigen::MatrixXd d2f1_kappa(const Rcpp::List &spde,int grid_size = 50, int Ns = 200,
                           double grid_limit = 4.0, int n_sess = 1) {
  if(grid_size < 20) {
    grid_size = 20;
    Rcout << "Grid size should be at least 20 for adequate coverage. Setting to 20 now." << std::endl;
  }
  // Load parameters
  Eigen::SparseMatrix<double> Cmat     = Eigen::SparseMatrix<double> (spde["Cmat"]);
  Eigen::SparseMatrix<double> Gmat     = Eigen::SparseMatrix<double> (spde["Gmat"]);
  Eigen::SparseMatrix<double> GtCinvG     = Eigen::SparseMatrix<double> (spde["GtCinvG"]);
  Eigen::MatrixXd CV, P_tilCV, GCGV, P_tilGCGV;
  Eigen::VectorXd diagP_tilCV, diagP_tilGCGV;
  double sumDiagP_tilCVkn = 0.0, sumDiagP_tilGCGV = 0.0;
  int n_spde = Cmat.rows(); // find the size of the mesh
  Eigen::MatrixXd out = Eigen::MatrixXd::Zero(grid_size, 2); // Create the output object
  double eps = 1e-8; // The level of precision that will be added to each grid step (grid starts at eps)
  // double grid_step = 2.5e-4; // How big should each step be? This is small when < 1, but larger afterwards
  double logkappa = -2.0;
  double grid_step = (grid_limit + 2.0) / grid_size;
  double kappa2 = std::pow(std::exp(logkappa),2.0), d1f1 = 0.0, d1f1_old = 0.0, d2f1 = 0.0, sumDiagP_tilCV; // start the grid for kappa2
  double kappa2_old = kappa2;
  Eigen::MatrixXd Vh = makeV(n_spde,Ns);
  Eigen::SparseMatrix<double> Qt(n_spde,n_spde); // initialize the Q_tilde matrix
  makeQt(&Qt, kappa2, spde);
  SimplicialLLT<Eigen::SparseMatrix<double>> cholQ;
  cholQ.analyzePattern(Qt);
  Eigen::MatrixXd P_til(n_spde, Ns);
  for(int i=0;i<(grid_size+1);i++) {
    makeQt(&Qt, kappa2, spde);
    cholQ.factorize(Qt);
    P_til = cholQ.solve(Vh);
    CV = Cmat * Vh;
    GCGV = GtCinvG * Vh;
    // Trace approximations w/ Q for CG
    // Trace of C * Q^-1
    P_tilCV = P_til.transpose() * CV;
    diagP_tilCV = P_tilCV.diagonal();
    sumDiagP_tilCV = diagP_tilCV.sum() / Ns;
    // Trace of GCG * Q^-1
    P_tilGCGV = P_til.transpose() * GCGV;
    diagP_tilGCGV = P_tilGCGV.diagonal();
    sumDiagP_tilGCGV = diagP_tilGCGV.sum() / Ns;
    d1f1 = sumDiagP_tilCVkn / 2.0 - sumDiagP_tilGCGV / (2.0 * kappa2 * kappa2);
    if(i > 0) {
      d2f1 = d1f1-d1f1_old;
      out((i-1),0) = (kappa2 + kappa2_old) / 2.0;
      out((i-1),1) = d2f1 * n_sess;
    }
    if(kappa2 > 1e-3) grid_step = 0.25;
    if(kappa2 > 1) grid_step = grid_limit / grid_size;
    kappa2_old = kappa2;
    // kappa2 += grid_step;
    logkappa += grid_step;
    kappa2 = std::pow(std::exp(logkappa),2.0);
    d1f1_old = d1f1;
  }
  return out;
}

double kappa2InitObj(double kappa2, double phi, const List &spde, Eigen::VectorXd beta_hat, double n_sess) {
  double lDQ = logDetQt(kappa2, spde, n_sess);
  Eigen::SparseMatrix<double> Cmat = Eigen::SparseMatrix<double> (spde["Cmat"]);
  int n_spde = Cmat.rows();
  Eigen::SparseMatrix<double> Qt(n_spde,n_spde);
  makeQt(&Qt, kappa2, spde);
  Eigen::VectorXd Qw(n_spde), wNs(n_spde);
  double wQw = 0.;
  for(int ns = 0; ns < n_sess; ns++) {
    wNs = beta_hat.segment(ns * n_spde, n_spde);
    Qw = Qt * wNs;
    wQw += wNs.transpose() * Qw;
  }
  // Rcout << "log_det_Q = " << lDQ << ", bQb = " << wQw << std::endl;
  wQw = wQw / (8. * M_PI * phi);
  lDQ = lDQ / 2.;
  double initObj = wQw - lDQ;
  return initObj;
}

double kappa2BrentInit(double lower, double upper, double phi, const List &spde, Eigen::VectorXd beta_hat, double n_sess) {
  // Define squared inverse of the golden ratio
  const double c = (3. - std::sqrt(5.)) / 2.;
  // Initialize local variables
  double a, b, d, e, p, q, r, u, v, w, x;
  double t2, fu, fv, fw, fx, xm, eps, tol1, tol3;
  eps = DBL_EPSILON; // machine precision
  tol1 = eps + 1.;
  eps = std::sqrt(eps);
  double tol = std::sqrt(eps);

  a = lower;
  b = upper;
  v = a + c*(b-a);
  x = v;
  x = v;

  d = 0.;
  e = 0.;
  // I don't know what these next three lines mean
  // fx = (*f)(x, info);
  fx = kappa2InitObj(x, phi, spde, beta_hat, n_sess);
  fv = fx;
  fw = fx;
  tol3 = tol / 3.;

  // Main for loop
  for(;;) {
    xm = (a+b)/2.;
    tol1 = eps * std::abs(x) + tol3;
    t2 = tol1 * 2.;
    // Check stopping criterion
    if (std::abs(x - xm) <= t2 - (b - a) / 2.) break;
    p = 0.;
    q = 0.;
    r = 0.;
    if (std::abs(e) > tol1) { //  fit parabola
      r = (x - w) * (fx - fv);
      q = (x - v) * (fx - fw);
      p = (x - v) * q - (x - w) * r;
      q = (q - r) * 2.;
      if (q > 0.) p = -p; else q = -q;
      r = e;
      e = d;
    }
    if (std::abs(p) >= std::abs(q * .5 * r) ||
        p <= q * (a - x) || p >= q * (b - x)) { /* a golden-section step */

      if (x < xm) e = b - x; else e = a - x;
      d = c * e;
    }
    else { /* a parabolic-interpolation step */

      d = p / q;
      u = x + d;

      /* f must not be evaluated too close to ax or bx */

      if (u - a < t2 || b - u < t2) {
        d = tol1;
        if (x >= xm) d = -d;
      }
    }

    /* f must not be evaluated too close to x */

    if (std::abs(d) >= tol1)
      u = x + d;
    else if (d > 0.)
      u = x + tol1;
    else
      u = x - tol1;

    // fu = (*f)(u, info);
    fu = kappa2InitObj(u, phi, spde, beta_hat, n_sess);

    /*  update  a, b, v, w, and x */

    if (fu <= fx) {
      if (u < x) b = x; else a = x;
      v = w;    w = x;   x = u;
      fv = fw; fw = fx; fx = fu;
    } else {
      if (u < x) a = u; else b = u;
      if (fu <= fw || w == x) {
        v = w; fv = fw;
        w = u; fw = fu;
      } else if (fu <= fv || v == x || v == w) {
        v = u; fv = fu;
      }
    }
  }
  // end of main loop
  // Rcout << "objective = " << fx << std::endl;
  return x;
}

double kappa2Obj(double kappa2, const Rcpp::List &spde, double a_star, double b_star, double n_sess) {
  double lDQ = logDetQt(kappa2, spde, n_sess);
  double out = a_star * kappa2 + b_star / kappa2 - lDQ;
  return out;
}

double kappa2Brent(double lower, double upper, const Rcpp::List &spde, double a_star, double b_star, double n_sess) {
  // Define squared inverse of the golden ratio
  const double c = (3. - std::sqrt(5.)) / 2.;
  // Initialize local variables
  double a, b, d, e, p, q, r, u, v, w, x;
  double t2, fu, fv, fw, fx, xm, eps, tol1, tol3;
  eps = DBL_EPSILON; // machine precision
  tol1 = eps + 1.;
  eps = std::sqrt(eps);
  double tol = std::sqrt(eps);

  a = lower;
  b = upper;
  v = a + c*(b-a);
  x = v;
  x = v;

  d = 0.;
  e = 0.;
  // I don't know what these next three lines mean
  // fx = (*f)(x, info);
  fx = kappa2Obj(x, spde, a_star, b_star, n_sess);
  fv = fx;
  fw = fx;
  tol3 = tol / 3.;

  // Main for loop
  for(;;) {
    xm = (a+b)/2.;
    tol1 = eps * std::abs(x) + tol3;
    t2 = tol1 * 2.;
    // Check stopping criterion
    if (std::abs(x - xm) <= t2 - (b - a) / 2.) break;
    p = 0.;
    q = 0.;
    r = 0.;
    if (std::abs(e) > tol1) { //  fit parabola
      r = (x - w) * (fx - fv);
      q = (x - v) * (fx - fw);
      p = (x - v) * q - (x - w) * r;
      q = (q - r) * 2.;
      if (q > 0.) p = -p; else q = -q;
      r = e;
      e = d;
    }
    if (std::abs(p) >= std::abs(q * .5 * r) ||
        p <= q * (a - x) || p >= q * (b - x)) { /* a golden-section step */

      if (x < xm) e = b - x; else e = a - x;
      d = c * e;
    }
    else { /* a parabolic-interpolation step */

      d = p / q;
      u = x + d;

      /* f must not be evaluated too close to ax or bx */

      if (u - a < t2 || b - u < t2) {
        d = tol1;
        if (x >= xm) d = -d;
      }
    }

    /* f must not be evaluated too close to x */

    if (std::abs(d) >= tol1)
      u = x + d;
    else if (d > 0.)
      u = x + tol1;
    else
      u = x - tol1;

    // fu = (*f)(u, info);
    fu = kappa2Obj(u, spde, a_star, b_star, n_sess);

    /*  update  a, b, v, w, and x */

    if (fu <= fx) {
      if (u < x) b = x; else a = x;
      v = w;    w = x;   x = u;
      fv = fw; fw = fx; fx = fu;
    } else {
      if (u < x) a = u; else b = u;
      if (fu <= fw || w == x) {
        v = w; fv = fw;
        w = u; fw = fu;
      } else if (fu <= fv || v == x || v == w) {
        v = u; fv = fu;
      }
    }
  }
  // end of main loop
  // Rcout << "objective = " << fx;
  return x;
}

//Global Control Variable
struct SquaremControl{
  int K=1;
  int method=3;//1,2,3 indicates the types of step length to be used in squarem1,squarem2, 4,5 for "rre" and "mpe" in cyclem1 and cyclem2,  standing for reduced-rank ("rre") or minimal-polynomial ("mpe") extrapolation.
  // K=1 must go with method=1,2 or 3
  // K>1 must go with method=4 or 5
  double mstep=4;
  int maxiter=1500;
  bool square=true;
  bool trace=true;//currently set to be true for debugging purpose
  double stepmin0=1;
  double stepmax0=1;
  double kr=1;
  double objfninc=1;//0 to enforce monotonicity, Inf for non-monotonic scheme, 1 for monotonicity far from solution and allows for non-monotonicity closer to solution
  double tol=1e-7;
} SquaremDefault;

//Output Struct
struct SquaremOutput{
  Eigen::VectorXd par;
  double valueobjfn;
  int iter=0;
  int pfevals=0;
  int objfevals=0;
  bool convergence=false;
} sqobj,sqobjnull;

Eigen::VectorXd init_fixptC(Eigen::VectorXd theta, Eigen::VectorXd w, List spde, double n_sess) {
  int n_spde = w.size();
  int start_idx;
  Eigen::VectorXd wNs(n_spde);
  n_spde = n_spde / n_sess;
  Eigen::VectorXd Qw(n_spde);
  double wQw = 0.;
  // theta(0) = kappa2BrentInit(0., 50., theta(1), spde, w, n_sess, tol);
  theta(0) = kappa2BrentInit(0., 50., theta(1), spde, w, n_sess);
  Eigen::SparseMatrix<double> Q(n_spde,n_spde);
  makeQt(&Q, theta(0), spde);
  for (int ns = 0; ns < n_sess; ns++) {
    start_idx = ns * n_spde;
    wNs = w.segment(start_idx, n_spde);
    Qw = Q * wNs;
    wQw += wNs.transpose() * Qw;
  }
  theta(1) = wQw / (4.0 * M_PI * n_spde * n_sess);
  return theta;
}

SquaremOutput init_squarem2(Eigen::VectorXd par, Eigen::VectorXd w, List spde, double n_sess, double tol){
  double res,parnorm,kres;
  Eigen::VectorXd pcpp,p1cpp,p2cpp,pnew,ptmp;
  Eigen::VectorXd q1,q2,sr2,sq2,sv2,srv;
  Eigen::VectorXd diffp1p, diffp2p1;
  double sr2_scalar,sq2_scalar,sv2_scalar,srv_scalar,alpha,stepmin,stepmax;
  int iter,feval;
  bool conv,extrap;
  stepmin=SquaremDefault.stepmin0;
  stepmax=SquaremDefault.stepmax0;
  if(SquaremDefault.trace){Rcout<<"Squarem-2"<<std::endl;}

  iter=1;pcpp=par;pnew=par;
  feval=0;conv=true;

  const long int parvectorlength=pcpp.size();

  while(feval<SquaremDefault.maxiter){
    //Step 1
    extrap = true;
    // try{p1cpp=fixptfn(pcpp);feval++;}
    try{p1cpp=init_fixptC(pcpp, w, spde, n_sess);feval++;}
    catch(...){
      Rcout<<"Error in fixptfn function evaluation";
      return sqobjnull;
    }

    // diffp1p = p1cpp - pcpp;
    // sr2_scalar = diffpp1p.squaredNorm();
    sr2_scalar=0;
    for (int i=0;i<parvectorlength;i++){sr2_scalar+=std::pow(p1cpp[i]-pcpp[i],2.0);}
    if(std::sqrt(sr2_scalar)<SquaremDefault.tol){break;}

    //Step 2
    try{p2cpp=init_fixptC(p1cpp,  w, spde, n_sess);feval++;}
    catch(...){
      Rcout<<"Error in fixptfn function evaluation";
      return sqobjnull;
    }
    // diffp2p1 = p2cpp - p1cpp;
    // sq2_scalar= diffp2p1.squaredNorm();
    sq2_scalar=0;
    for (int i=0;i<parvectorlength;i++){sq2_scalar+=std::pow(p2cpp[i]-p1cpp[i],2.0);}
    sq2_scalar=std::sqrt(sq2_scalar);
    if (sq2_scalar<SquaremDefault.tol){break;}
    res=sq2_scalar;

    // p2M2p1Pp = p2cpp - 2. * p1cpp + pcpp;
    // sv2_scalar = p2M2p1Pp.squaredNorm();
    sv2_scalar=0;
    for (int i=0;i<parvectorlength;i++){sv2_scalar+=std::pow(p2cpp[i]-2*p1cpp[i]+pcpp[i],2.0);}
    // p1Mp = p1cpp - pcpp;
    // p2M2p1PpTp1Mp = p2M2p1Pp * p1Mp;
    // srv_scalar = p2M2p1PpTp1Mp.squaredNorm();
    srv_scalar=0;
    for (int i=0;i<parvectorlength;i++){srv_scalar+=(p2cpp[i]-2*p1cpp[i]+pcpp[i])*(p1cpp[i]-pcpp[i]);}
    //std::cout<<"sr2,sv2,srv="<<sr2_scalar<<","<<sv2_scalar<<","<<srv_scalar<<std::endl;//debugging

    //Step 3 Proposing new value
    switch (SquaremDefault.method){
    case 1: alpha= -srv_scalar/sv2_scalar;
    case 2: alpha= -sr2_scalar/srv_scalar;
    case 3: alpha= std::sqrt(sr2_scalar/sv2_scalar);
    }

    alpha=std::max(stepmin,std::min(stepmax,alpha));
    //std::cout<<"alpha="<<alpha<<std::endl;//debugging
    for (int i=0;i<parvectorlength;i++){pnew[i]=pcpp[i]+2.0*alpha*(p1cpp[i]-pcpp[i])+alpha*alpha*(p2cpp[i]-2.0*p1cpp[i]+pcpp[i]);}
    // pnew = pcpp + 2.0*alpha*q1 + alpha*alpha*(q2-q1);

    //Step 4 stabilization
    if(std::abs(alpha-1)>0.01){
      try{ptmp=init_fixptC(pnew,  w, spde, n_sess);feval++;}
      catch(...){
        pnew=p2cpp;
        if(alpha==stepmax){
          stepmax=std::max(SquaremDefault.stepmax0,stepmax/SquaremDefault.mstep);
        }
        alpha=1;
        extrap=false;
        if(alpha==stepmax){stepmax=SquaremDefault.mstep*stepmax;}
        if(stepmin<0 && alpha==stepmin){stepmin=SquaremDefault.mstep*stepmin;}
        pcpp=pnew;
        if(SquaremDefault.trace){Rcout<<"Residual: "<<res<<"  Extrapolation: "<<extrap<<"  Steplength: "<<alpha<<std::endl;}
        iter++;
        continue;//next round in while loop
      }
      res=0;
      for (int i=0;i<parvectorlength;i++){res+=std::pow(ptmp[i]-pnew[i],2.0);}
      res=std::sqrt(res);
      parnorm=0;
      for (int i=0;i<parvectorlength;i++){parnorm+=std::pow(p2cpp[i],2.0);}
      parnorm=std::sqrt(parnorm/parvectorlength);
      kres=SquaremDefault.kr*(1+parnorm)+sq2_scalar;
      if(res <= kres){
        pnew=ptmp;
      }else{
        pnew=p2cpp;
        if(alpha==stepmax){stepmax=SquaremDefault.mstep*stepmax;}
        alpha=1;
        extrap=false;
      }
    }

    if(alpha==stepmax){stepmax=SquaremDefault.mstep*stepmax;}
    if(stepmin<0 && alpha==stepmin){stepmin=SquaremDefault.mstep*stepmin;}

    pcpp=pnew;
    if(SquaremDefault.trace) {
      Rcout<<"Residual: "<<res<<"  Extrapolation: "<<extrap<<"  Steplength: "<<alpha<<std::endl;
      Rcout<<"Hyperparameter values: "<<pcpp.transpose()<<std::endl;
      }
    iter++;
  }

  if (feval >= SquaremDefault.maxiter){conv=false;}

  //assigning values
  sqobj.par=pcpp;
  sqobj.valueobjfn=NAN;
  sqobj.iter=iter;
  sqobj.pfevals=feval;
  sqobj.objfevals=0;
  sqobj.convergence=conv;
  return(sqobj);
}

//' Find the initial values of kappa2 and phi
//'
//' @param theta a vector of length two containing the range and scale parameters
//'   kappa2 and phi, in that order
//' @param spde a list containing the sparse matrix elements Cmat, Gmat, and GtCinvG
//' @param w the beta_hat estimates for a single task
//' @param n_sess the number of sessions
//' @param tol the stopping rule tolerance
//' @param verbose (logical) Should intermediate output be displayed?
//' @export
// [[Rcpp::export(rng = false)]]
Eigen::VectorXd initialKP(Eigen::VectorXd theta, List spde, Eigen::VectorXd w,
                          double n_sess, double tol, bool verbose) {
  int n_spde = w.size();
  n_spde = n_spde / n_sess;
  // Set up implementation without EM
  // double eps = tol + 1;
  // Eigen::VectorXd new_theta(theta.size()), diffTheta(theta.size());
  // while(eps > tol) {
  //   new_theta = init_fixptC(theta, w, spde, n_sess);
  //   diffTheta = new_theta - theta;
  //   eps = diffTheta.squaredNorm();
  //   eps = sqrt(eps);
  //   theta = new_theta;
  // }
  // Implementation with in EM
  SquaremOutput SQ_out;
  SquaremDefault.tol = tol;
  SquaremDefault.trace = verbose;
  SQ_out = init_squarem2(theta, w, spde, n_sess, tol);
  // Rcout << "valueobjfn = " << SQ_out.valueobjfn << ", iter = " << SQ_out.iter;
  // Rcout << ", fpevals = " << SQ_out.pfevals << ", objevals = " << SQ_out.objfevals;
  // Rcout << ", convergence = " << SQ_out.convergence << std::endl;
  theta= SQ_out.par;
  return theta;
}

/*
 B of size n1 x n2
 Set A(i:i+n1,j:j+n2) = B (update)
 */
void setSparseBlock_update(SparseMatrix<double,0,int>* A,int i, int j, SparseMatrix<double,0,int>& B)
{
  for (int k=0; k<B.outerSize(); ++k) {
    for (SparseMatrix<double,0,int>::InnerIterator it(B,k); it; ++it) {
      A->coeffRef(it.row()+i, it.col()+j) = it.value();
    }
  }
}

/*
 B of size n1 x n2
 Set A(i:i+n1,j:j+n2) = B (update)
 */
void setSparseCol_update(SparseMatrix<double,0,int>* A,Rcpp::List B_list)
{
  int K = B_list.length();;
  int startCol=0, stopCol=0, Bk_cols;
  for(int k=0;k<K;k++) {
    Rcout << "k = " << k << std::endl;
    Eigen::SparseMatrix<double> Bk = B_list(k);
    Bk_cols = Bk.cols();
    stopCol = startCol + Bk_cols;
    for(int j=startCol;j<stopCol;j++){
      Rcout << "j = " << j << ", ";
      A->startVec(j);
      for(Eigen::SparseMatrix<double,0,int>::InnerIterator it(Bk,j-startCol); it; ++it) {
        A->insertBack(it.row()+startCol,j) = it.value();
      }
    }
    startCol = stopCol + 1;
  }
  A->finalize();
}

Eigen::SparseMatrix<double> sparseBdiag(Rcpp::List B_list)
{
  int K = B_list.length();
   Eigen::VectorXi B_cols(K);
  for(int k=0;k<K;k++) {
    Eigen::SparseMatrix<double> Bk = B_list(k);
    B_cols[k] = Bk.cols();
  }
  int sumCols = B_cols.sum();
  Eigen::SparseMatrix<double> A(sumCols,sumCols);
  int startCol=0, stopCol=0, Bk_cols;
  for(int k=0;k<K;k++) {
    Eigen::SparseMatrix<double> Bk = B_list(k);
    Bk_cols = Bk.cols();
    stopCol = startCol + Bk_cols;
    for(int j=startCol;j<stopCol;j++){
      A.startVec(j);
      for(Eigen::SparseMatrix<double,0,int>::InnerIterator it(Bk,j-startCol); it; ++it) {
        A.insertBack(it.row()+startCol,j) = it.value();
      }
    }
    startCol = stopCol;
  }
  A.finalize();
  return A;
}

double emObj(Eigen::VectorXd theta, const Eigen::SparseMatrix<double> A,
             Eigen::SparseMatrix<double> QK,
             SimplicialLLT<Eigen::SparseMatrix<double>> &cholSigInv,
             const Eigen::VectorXd XpsiY, const Eigen::SparseMatrix<double> Xpsi,
             const int Ns, const Eigen::VectorXd y, const List spde) {
  // Bring in the spde matrices
  Eigen::SparseMatrix<double> Cmat     = Eigen::SparseMatrix<double> (spde["Cmat"]);
  Eigen::SparseMatrix<double> Gmat     = Eigen::SparseMatrix<double> (spde["Gmat"]);
  Eigen::SparseMatrix<double> GtCinvG     = Eigen::SparseMatrix<double> (spde["GtCinvG"]);
  // Grab metadata
  int K = (theta.size() - 1) / 2;
  int sig2_ind = theta.size() - 1;
  int nKs = A.rows();
  int ySize = y.size();
  int n_spde = Cmat.rows();
  double n_sess = nKs / (n_spde * K);
  Eigen::MatrixXd Vh = makeV(nKs,Ns);
  // Initialize objects
  Eigen::SparseMatrix<double> AdivS2(nKs,nKs), Sig_inv(nKs,nKs), Qk(n_spde, n_spde);
  for(int k = 0; k < K ; k++) {
    makeQt(&Qk, theta(k), spde);
    Qk = Qk / (4.0 * M_PI * theta(k + K));
    for(int ns = 0; ns < n_sess; ns++) {
      int start_i = k * n_spde + ns * K * n_spde;
      setSparseBlock_update(&QK, start_i, start_i, Qk);
    }
  }
  AdivS2 = A / theta[sig2_ind];
  Sig_inv = QK + AdivS2;
  cholSigInv.factorize(Sig_inv);
  Eigen::MatrixXd P = cholSigInv.solve(Vh);
  Eigen::MatrixXd PqVh = P.transpose() * QK;
  Eigen::VectorXd diagPqVh = PqVh.diagonal();
  double TrSigQ = diagPqVh.sum() / Ns;
  Eigen::VectorXd m = XpsiY / theta(sig2_ind);
  Eigen::VectorXd mu = cholSigInv.solve(m);
  Eigen::MatrixXd muTmu = mu.transpose() * mu;
  Eigen::MatrixXd QmuTmu = QK * muTmu;
  double TrQmuTmu = QmuTmu.diagonal().sum();
  SimplicialLDLT<Eigen::SparseMatrix<double>> cholQ(QK);
  double lDQ = cholQ.vectorD().array().log().sum();
  Eigen::VectorXd XB = Xpsi * mu;
  Eigen::VectorXd y_til = y - XB;
  double ytil2 = y_til.transpose() * y_til;
  double llik = -ySize * log(theta[sig2_ind]);
  llik = llik - ytil2 / theta[sig2_ind];
  llik = llik - lDQ;
  llik = llik - TrSigQ - TrQmuTmu;
  llik = llik / 2.;
  return llik;
}

#include <time.h>

Eigen::VectorXd theta_fixpt(Eigen::VectorXd theta, const Eigen::SparseMatrix<double> A,
                            Eigen::SparseMatrix<double> QK, SimplicialLLT<Eigen::SparseMatrix<double>> &cholSigInv,
                            const Eigen::VectorXd XpsiY, const Eigen::SparseMatrix<double> Xpsi,
                            const int Ns, const Eigen::VectorXd y,
                            const double yy, const List spde, double tol) {
  clock_t time_start, time_setup, time_QK, time_mu, time_sigma2, time_kappa_phi;
  // Rcout << "Starting fixed-point updates" << std::endl;
  time_start = clock();
  // Bring in the spde matrices
  Eigen::SparseMatrix<double> Cmat     = Eigen::SparseMatrix<double> (spde["Cmat"]);
  Eigen::SparseMatrix<double> Gmat     = Eigen::SparseMatrix<double> (spde["Gmat"]);
  Eigen::SparseMatrix<double> GtCinvG     = Eigen::SparseMatrix<double> (spde["GtCinvG"]);
  // Grab metadata
  int K = (theta.size() - 1) / 2;
  int sig2_ind = theta.size() - 1;
  int nKs = A.rows();
  int ySize = y.size();
  int n_spde = Cmat.rows();
  double n_sess = nKs / (n_spde * K);
  // int Ns = Vh.cols();
  Eigen::MatrixXd Vh = makeV(nKs,Ns);
  // Rcout << "dim(A)" << A.rows() << " x " << A.cols() << ", dim(Vh) = " << Vh.rows() << " x " << Vh.cols() << std::endl;
  Eigen::MatrixXd Avh = A * Vh;
  // Initialize objects
  Eigen::SparseMatrix<double> AdivS2(nKs,nKs), Sig_inv(nKs,nKs), Qk(n_spde, n_spde);
  Eigen::VectorXd theta_new = theta;
  Eigen::VectorXd muKns(n_spde), Cmu(n_spde), Gmu(n_spde), diagPCVkn(Ns);
  Eigen::VectorXd diagPGVkn(Ns), GCGmu(n_spde), diagPGCGVkn(Ns);
  Eigen::MatrixXd Pkn(n_spde, Ns), Vkn(n_spde, Ns), CVkn(n_spde, Ns), PCVkn(Ns, Ns);
  Eigen::MatrixXd GVkn(n_spde, Ns), PGVkn(Ns, Ns), GCGVkn(n_spde, Ns), PGCGVkn(Ns,Ns);
  double a_star, b_star, muCmu, muGmu, sumDiagPCVkn, sumDiagPGVkn, muGCGmu;
  double sumDiagPGCGVkn, phi_partA, phi_partB, phi_partC, new_kappa2, phi_new;
  double phi_denom = 4.0 * M_PI * n_spde * n_sess;
  int idx_start;
  time_setup = clock();
  // Begin update
  // for(int k = 0; k < K ; k++) {
  //   makeQt(&Qk, theta(k), spde);
  //   Qk = Qk / (4.0 * M_PI * theta(k + K));
  //   for(int ns = 0; ns < n_sess; ns++) {
  //     int start_i = k * n_spde + ns * K * n_spde;
  //     setSparseBlock_update(&QK, start_i, start_i, Qk);
  //   }
  // }
  Rcpp::List Q_list(K * n_sess);
  int list_idx;
  for(int ns=0;ns<n_sess;ns++) {
    for(int k = 0; k < K ; k++) {
      list_idx = ns*K + k;
      makeQt(&Qk, theta(k), spde);
      Qk = Qk / (4.0 * M_PI * theta(k + K));
      Q_list(list_idx) =  Qk;
    }
  }
  QK = sparseBdiag(Q_list);
  time_QK = clock();
  // setSparseCol_update(&QK,Q_list);

  // for(int k = 0; k < K ; k++) {
  //   makeQt(&Qk, theta(k), spde);
  //   Qk = Qk / (4.0 * M_PI * theta(k + K));
  //   for(int ns = 0; ns < n_sess; ns++) {
  //     int start_i = k * n_spde + ns * K * n_spde;
  //     setSparseCol_update(&QK, start_i, Qk);
  //   }
  // }
  AdivS2 = A / theta[sig2_ind];
  Sig_inv = QK + AdivS2;
  cholSigInv.factorize(Sig_inv);
  Eigen::VectorXd m = XpsiY / theta(sig2_ind);
  Eigen::VectorXd mu = cholSigInv.solve(m);
  time_mu = clock();
  // Rcout << "First 6 values of mu: " << mu.segment(0,6).transpose() << std::endl;
  // Solve for sigma_2
  Eigen::VectorXd XpsiMu = Xpsi * mu;
  Eigen::MatrixXd P = cholSigInv.solve(Vh);
  Eigen::MatrixXd PaVh = P.transpose() * Avh;
  Eigen::VectorXd diagPaVh = PaVh.diagonal();
  double TrSigA = diagPaVh.sum() / Ns;
  Eigen::VectorXd Amu = A * mu;
  double muAmu = mu.transpose() * Amu;
  double TrAEww = muAmu + TrSigA;
  // Rcout << "TrAEww = " << TrAEww << std::endl;
  double yXpsiMu = y.transpose() * XpsiMu;
  theta_new[sig2_ind] = (yy - 2 * yXpsiMu + TrAEww) / ySize;
  time_sigma2 = clock();
  // Update kappa2 and phi by task
  for(int k = 0; k < K; k++) {
    a_star = 0.0;
    b_star = 0.0;
    muCmu = 0.0;
    muGmu = 0.0;
    muGCGmu = 0.0;
    sumDiagPCVkn = 0.0;
    sumDiagPGVkn = 0.0;
    sumDiagPGCGVkn = 0.0;
    for(int ns = 0; ns < n_sess; ns++) {
      idx_start = k * n_spde + ns * K * n_spde;
      // idx_stop = idx_start + n_spde;
      muKns = mu.segment(idx_start,n_spde);
      // muCmu
      Cmu = Cmat * muKns;
      muCmu += muKns.transpose() * Cmu;
      // muGmu
      Gmu = Gmat * muKns;
      muGmu += muKns.transpose() * Gmu;
      // muGCGmu
      GCGmu = GtCinvG * muKns;
      muGCGmu += muKns.transpose() * GCGmu;
      // Trace approximations w/ Sigma
      Pkn = P.block(idx_start, 0, n_spde, Ns);
      Vkn = Vh.block(idx_start, 0, n_spde, Ns);
      // Trace of C*Sigma
      CVkn = Cmat * Vkn;
      PCVkn = Pkn.transpose() * CVkn;
      diagPCVkn = PCVkn.diagonal();
      sumDiagPCVkn += diagPCVkn.sum();
      // Trace of G*Sigma
      GVkn = Gmat * Vkn;
      PGVkn = Pkn.transpose() * GVkn;
      diagPGVkn = PGVkn.diagonal();
      sumDiagPGVkn += diagPGVkn.sum();
      // Trace of GCG*Sigma
      GCGVkn = GtCinvG * Vkn;
      PGCGVkn = Pkn.transpose() * GCGVkn;
      diagPGCGVkn = PGCGVkn.diagonal();
      sumDiagPGCGVkn += diagPGCGVkn.sum();
    }
    sumDiagPCVkn = sumDiagPCVkn / Ns;
    sumDiagPGVkn = sumDiagPGVkn / Ns;
    sumDiagPGCGVkn = sumDiagPGCGVkn / Ns;
    // Update kappa2
    // Rcout << "muCmu = " << muCmu << ", muGCGmu = " << muGCGmu;
    // Rcout << ", TrCSig = " << sumDiagPCVkn << ", TrGCGSig = " << sumDiagPGCGVkn << std::endl;
    // Rcout << "head(diagPCV) = " << diagPCVkn.segment(0,6).transpose() << ", head(diagPGCGV) = " << diagPGCGVkn.segment(0,6).transpose() << std::endl;
    a_star = (muCmu + sumDiagPCVkn) / (4.0 * M_PI * theta[k + K]);
    b_star = (muGCGmu + sumDiagPGCGVkn) / (4.0 * M_PI * theta[k + K]);
    // Rcout << "k = " << k << " a_star = " << a_star << " b_star = " << b_star << std::endl;
    new_kappa2 = kappa2Brent(0., 3000., spde, a_star, b_star, n_sess);
    // Rcout << ", new_kappa2 = " << new_kappa2 << std::endl;
    theta_new[k] = new_kappa2;
    // Update phi
    phi_partA = sumDiagPCVkn + muCmu;
    phi_partA = phi_partA * new_kappa2;
    phi_partB = sumDiagPGVkn + muGmu;
    phi_partB = 2 * phi_partB;
    phi_partC = sumDiagPGCGVkn + muGCGmu;
    phi_partC = phi_partC / new_kappa2;
    double TrQEww = phi_partA + phi_partB + phi_partC;
    // Rcout << "TrQEww = " << TrQEww << std::endl;
    phi_new = TrQEww / phi_denom;
    theta_new[k + K] = phi_new;
  }
  time_kappa_phi = clock();
  // double time_dif_setup, time_dif_QK, time_dif_mu, time_dif_sigma2, time_dif_kappa_phi;
  // time_dif_setup = (double)(time_setup - time_start)/CLOCKS_PER_SEC;
  // time_dif_QK = (double)(time_QK - time_setup)/CLOCKS_PER_SEC;
  // time_dif_mu = (double)(time_mu - time_QK)/CLOCKS_PER_SEC;
  // time_dif_sigma2 = (double)(time_sigma2 - time_mu)/CLOCKS_PER_SEC;
  // time_dif_kappa_phi = (double)(time_kappa_phi - time_sigma2)/CLOCKS_PER_SEC;
  // Rcout << "Setup: " << time_dif_setup << ", QK: " << time_dif_QK << ", mu: " << time_dif_mu << ", sigma2: " << time_dif_sigma2 << ", kappa & phi: " << time_dif_kappa_phi << std::endl;
  return(theta_new);
}

Eigen::VectorXd theta_fixpt_CG(Eigen::VectorXd theta, const Eigen::SparseMatrix<double> A,
                            Eigen::SparseMatrix<double> QK, SimplicialLLT<Eigen::SparseMatrix<double>> &cholSigInv,
                            SimplicialLLT<Eigen::SparseMatrix<double>> &cholQ,
                            const Eigen::VectorXd XpsiY, const Eigen::SparseMatrix<double> Xpsi,
                            const int Ns, const Eigen::VectorXd y, const Eigen::MatrixXd d2f1,
                            const double yy, const List spde, double tol) {
  clock_t time_start, time_setup, time_QK, time_mu, time_sigma2, time_kappa_phi;
  // Rcout << "Starting fixed-point updates" << std::endl;
  time_start = clock();
  // Bring in the spde matrices
  Eigen::SparseMatrix<double> Cmat     = Eigen::SparseMatrix<double> (spde["Cmat"]);
  Eigen::SparseMatrix<double> Gmat     = Eigen::SparseMatrix<double> (spde["Gmat"]);
  Eigen::SparseMatrix<double> GtCinvG     = Eigen::SparseMatrix<double> (spde["GtCinvG"]);
  // Grab metadata
  int K = (theta.size() - 1) / 2;
  int sig2_ind = theta.size() - 1;
  int nKs = A.rows();
  int ySize = y.size();
  int n_spde = Cmat.rows();
  double n_sess = nKs / (n_spde * K);
  // int Ns = Vh.cols();
  Eigen::MatrixXd Vh = makeV(nKs,Ns);
  // Rcout << "dim(A)" << A.rows() << " x " << A.cols() << ", dim(Vh) = " << Vh.rows() << " x " << Vh.cols() << std::endl;
  Eigen::MatrixXd Avh = A * Vh;
  // Initialize objects
  Eigen::SparseMatrix<double> AdivS2(nKs,nKs), Sig_inv(nKs,nKs), Qk(n_spde, n_spde);
  Eigen::VectorXd theta_new = theta;
  Eigen::VectorXd muKns(n_spde), Cmu(n_spde), Gmu(n_spde), diagPCVkn(Ns);
  Eigen::VectorXd diagPGVkn(Ns), GCGmu(n_spde), diagPGCGVkn(Ns);
  Eigen::VectorXd diagP_tilCVkn(Ns), diagP_tilGCGVkn(Ns), diff_kappa2;
  Eigen::MatrixXd Pkn(n_spde, Ns), Vkn(n_spde, Ns), CVkn(n_spde, Ns), PCVkn(Ns, Ns);
  Eigen::MatrixXd GVkn(n_spde, Ns), PGVkn(Ns, Ns), GCGVkn(n_spde, Ns), PGCGVkn(Ns,Ns);
  Eigen::MatrixXd P_tilkn(n_spde,Ns), P_tilCVkn(Ns,Ns), P_tilGCGVkn(Ns,Ns);
  double a_star = 0.0, b_star = 0.0, muCmu = 0.0, muGmu = 0.0;
  double sumDiagPCVkn = 0.0, sumDiagPGVkn = 0.0, muGCGmu = 0.0;
  double sumDiagPGCGVkn = 0.0, phi_partA = 0.0, phi_partB = 0.0, phi_partC = 0.0;
  double new_kappa2 = 0.0, phi_new = 0.0;
  double phi_denom = 4.0 * M_PI * n_spde * n_sess, d1f1, d1f2, d2f2;
  double sumDiagP_tilCVkn = 0.0, sumDiagP_tilGCGVkn = 0.0, d1f= 0.0;
  int idx_start, d2_grid_size = d2f1.rows();
  time_setup = clock();
  // Begin update
  // for(int k = 0; k < K ; k++) {
  //   makeQt(&Qk, theta(k), spde);
  //   Qk = Qk / (4.0 * M_PI * theta(k + K));
  //   for(int ns = 0; ns < n_sess; ns++) {
  //     int start_i = k * n_spde + ns * K * n_spde;
  //     setSparseBlock_update(&QK, start_i, start_i, Qk);
  //   }
  // }
  Rcpp::List Q_list(K * n_sess);
  int list_idx;
  for(int ns=0;ns<n_sess;ns++) {
    for(int k = 0; k < K ; k++) {
      list_idx = ns*K + k;
      makeQt(&Qk, theta(k), spde);
      Qk = Qk / (4.0 * M_PI * theta(k + K));
      Q_list(list_idx) =  Qk;
    }
  }
  QK = sparseBdiag(Q_list);
  time_QK = clock();
  // setSparseCol_update(&QK,Q_list);

  // for(int k = 0; k < K ; k++) {
  //   makeQt(&Qk, theta(k), spde);
  //   Qk = Qk / (4.0 * M_PI * theta(k + K));
  //   for(int ns = 0; ns < n_sess; ns++) {
  //     int start_i = k * n_spde + ns * K * n_spde;
  //     setSparseCol_update(&QK, start_i, Qk);
  //   }
  // }
  AdivS2 = A / theta[sig2_ind];
  Sig_inv = QK + AdivS2;
  cholSigInv.factorize(Sig_inv);
  cholQ.factorize(QK); // for CG
  Eigen::VectorXd m = XpsiY / theta(sig2_ind);
  Eigen::VectorXd mu = cholSigInv.solve(m);
  time_mu = clock();
  // Rcout << "First 6 values of mu: " << mu.segment(0,6).transpose() << std::endl;
  // Solve for sigma_2
  Eigen::VectorXd XpsiMu = Xpsi * mu;
  Eigen::MatrixXd P = cholSigInv.solve(Vh);
  Eigen::MatrixXd P_til = cholQ.solve(Vh);
  Eigen::MatrixXd PaVh = P.transpose() * Avh;
  Eigen::VectorXd diagPaVh = PaVh.diagonal();
  double TrSigA = diagPaVh.sum() / Ns;
  Eigen::VectorXd Amu = A * mu;
  double muAmu = mu.transpose() * Amu;
  double TrAEww = muAmu + TrSigA;
  // Rcout << "TrAEww = " << TrAEww << std::endl;
  double yXpsiMu = y.transpose() * XpsiMu;
  theta_new[sig2_ind] = (yy - 2 * yXpsiMu + TrAEww) / ySize;
  time_sigma2 = clock();
  // Update kappa2 and phi by task
  for(int k = 0; k < K; k++) {
    if(shared) {
      a_star = 0.0;
      b_star = 0.0;
      muCmu = 0.0;
      muGmu = 0.0;
      muGCGmu = 0.0;
      sumDiagPCVkn = 0.0;
      sumDiagPGVkn = 0.0;
      sumDiagPGCGVkn = 0.0;
    }
    for(int ns = 0; ns < n_sess; ns++) {
      idx_start = k * n_spde + ns * K * n_spde;
      // idx_stop = idx_start + n_spde;
      muKns = mu.segment(idx_start,n_spde);
      // muCmu
      Cmu = Cmat * muKns;
      muCmu += muKns.transpose() * Cmu;
      // muGmu
      Gmu = Gmat * muKns;
      muGmu += muKns.transpose() * Gmu;
      // muGCGmu
      GCGmu = GtCinvG * muKns;
      muGCGmu += muKns.transpose() * GCGmu;
      // Trace approximations w/ Sigma
      Pkn = P.block(idx_start, 0, n_spde, Ns);
      Vkn = Vh.block(idx_start, 0, n_spde, Ns);
      // Trace of C*Sigma
      CVkn = Cmat * Vkn;
      PCVkn = Pkn.transpose() * CVkn;
      diagPCVkn = PCVkn.diagonal();
      sumDiagPCVkn += diagPCVkn.sum();
      // Trace of G*Sigma
      GVkn = Gmat * Vkn;
      PGVkn = Pkn.transpose() * GVkn;
      diagPGVkn = PGVkn.diagonal();
      sumDiagPGVkn += diagPGVkn.sum();
      // Trace of GCG*Sigma
      GCGVkn = GtCinvG * Vkn;
      PGCGVkn = Pkn.transpose() * GCGVkn;
      diagPGCGVkn = PGCGVkn.diagonal();
      sumDiagPGCGVkn += diagPGCGVkn.sum();
      // Trace approximations w/ Q for CG
      P_tilkn = P_til.block(idx_start, 0, n_spde, Ns);
      // Trace of C * Q^-1
      P_tilCVkn = P_tilkn.transpose() * CVkn;
      diagP_tilCVkn = P_tilCVkn.diagonal();
      sumDiagP_tilCVkn += diagP_tilCVkn.sum();
      // Trace of GCG * Q^-1
      P_tilGCGVkn = P_tilkn.transpose() * GCGVkn;
      diagP_tilGCGVkn = P_tilGCGVkn.diagonal();
      sumDiagP_tilGCGVkn += diagP_tilGCGVkn.sum();
    }
    sumDiagPCVkn = sumDiagPCVkn / Ns; // Approximate Tr(CSigma)
    sumDiagPGVkn = sumDiagPGVkn / Ns; // Approximate Tr(GSigma)
    sumDiagPGCGVkn = sumDiagPGCGVkn / Ns; // Approximate Tr(GCGSigma)
    sumDiagP_tilCVkn = sumDiagP_tilCVkn / Ns; // Approximate Tr(CQ^-1)
    sumDiagP_tilGCGVkn = sumDiagP_tilGCGVkn / Ns; // Approximate Tr(GCGQ^-1)
    // Update kappa2
    // Rcout << "muCmu = " << muCmu << ", muGCGmu = " << muGCGmu;
    // Rcout << ", TrCSig = " << sumDiagPCVkn << ", TrGCGSig = " << sumDiagPGCGVkn << std::endl;
    // Rcout << "head(diagPCV) = " << diagPCVkn.segment(0,6).transpose() << ", head(diagPGCGV) = " << diagPGCGVkn.segment(0,6).transpose() << std::endl;
    // a_star = (muCmu + sumDiagPCVkn) / (4.0 * M_PI * theta[k + K]);
    // b_star = (muGCGmu + sumDiagPGCGVkn) / (4.0 * M_PI * theta[k + K]);
    // Rcout << "k = " << k << " a_star = " << a_star << " b_star = " << b_star << std::endl;
    // new_kappa2 = kappa2Brent(0., 50., spde, a_star, b_star, n_sess);
    // CG update for kappa2
    d1f1 = sumDiagP_tilCVkn / 2.0 - sumDiagP_tilGCGVkn / (2.0 * theta[k] * theta[k]);
    d1f2 = sumDiagPCVkn + muCmu + sumDiagPGCGVkn / (theta[k] * theta[k]) - muGCGmu / (theta[k] * theta[k]);
    d1f2 = d1f2 / -(8 * M_PI * theta[k + K]);
    d1f = d1f1 + d1f2;
    // Using interpolation/extrapolation to estimate the value of d2f1
    // Make a matrix with three columns to include the values of the
    // absolute differences between the current value for kappa2 and the
    // gridded values from the matrix that includes the precomputed numerical
    // approximation to the second derivative.
    Eigen::MatrixXd kappaDiff_d2f1 = Eigen::MatrixXd::Zero(d2f1.rows(),3);
    kappaDiff_d2f1.rightCols(2) = d2f1;
    for(int j=0;j<d2_grid_size;j++) {
      kappaDiff_d2f1(j,0) = std::abs(theta[k] - d2f1(j,0)); // absolute difference
    }
    // Now reorder that matrix by the absolute differences
    eigen_sort_rows_by_head(kappaDiff_d2f1);
    // Calculate the slope and intercept of the line that passes through the
    // two nearest points
    double interp_slope = (kappaDiff_d2f1(1,2) - kappaDiff_d2f1(0,2)) / (kappaDiff_d2f1(1,1) - kappaDiff_d2f1(0,1));
    double interp_intercept = kappaDiff_d2f1(0,2) - interp_slope * kappaDiff_d2f1(0,1);
    double interp_d2f1 = interp_intercept + interp_slope * theta[k];
    // BEGIN NOT USED
    // int lowest_idx = 2;
    // Eigen::VectorXd in_vec = diff_kappa2, out_vec(lowest_idx);
    // for(int l=0;l<lowest_idx;l++) {
    //   int min_idx =0;
    //   for(int j=1;j<d2_grid_size;j++) {
    //     if(diff_kappa2[j] < diff_kappa2[min_idx]) min_idx = j;
    //   }
    //   out_vec[l] = min_idx;
    //   diff_kappa2 =
    // }
    // kappa_lower =
    // d2f1_a =
    // END NOT USED
    double d2f2 = sumDiagPGCGVkn + muGCGmu;
    d2f2 = d2f2 / (-4 * M_PI * theta[k + K] * std::pow(theta[k],3));
    double d2f = interp_d2f1 + d2f2;
    new_kappa2 = theta[k] -  d1f / d2f; // Good ol' fashioned Newton's method
    if(new_kappa2 < 1e-8) new_kappa2 = 1e-8;
    if(new_kappa2 > 1e2) new_kappa2 = 1e2;
    // Rcout << ", new_kappa2 = " << new_kappa2 << std::endl;
    theta_new[k] = new_kappa2;
    // Update phi
    phi_partA = sumDiagPCVkn + muCmu;
    phi_partA = phi_partA * new_kappa2;
    phi_partB = sumDiagPGVkn + muGmu;
    phi_partB = 2 * phi_partB;
    phi_partC = sumDiagPGCGVkn + muGCGmu;
    phi_partC = phi_partC / new_kappa2;
    double TrQEww = phi_partA + phi_partB + phi_partC;
    // Rcout << "TrQEww = " << TrQEww << std::endl;
    phi_new = TrQEww / phi_denom;
    theta_new[k + K] = phi_new;
  }
  time_kappa_phi = clock();
  // double time_dif_setup, time_dif_QK, time_dif_mu, time_dif_sigma2, time_dif_kappa_phi;
  // time_dif_setup = (double)(time_setup - time_start)/CLOCKS_PER_SEC;
  // time_dif_QK = (double)(time_QK - time_setup)/CLOCKS_PER_SEC;
  // time_dif_mu = (double)(time_mu - time_QK)/CLOCKS_PER_SEC;
  // time_dif_sigma2 = (double)(time_sigma2 - time_mu)/CLOCKS_PER_SEC;
  // time_dif_kappa_phi = (double)(time_kappa_phi - time_sigma2)/CLOCKS_PER_SEC;
  // Rcout << "Setup: " << time_dif_setup << ", QK: " << time_dif_QK << ", mu: " << time_dif_mu << ", sigma2: " << time_dif_sigma2 << ", kappa & phi: " << time_dif_kappa_phi << std::endl;
  return(theta_new);
}

// #include <iostream>
// #include <algorithm>
// #include <cmath>
// #include <math.h>
// #include <vector>
// #include <numeric>

// using namespace std;

// Eigen::VectorXd theta_fixptPARDISO(Eigen::VectorXd theta, const Eigen::SparseMatrix<double> A,
//                                    Eigen::SparseMatrix<double> QK, Eigen::PardisoLLT<Eigen::SparseMatrix<double,Eigen::RowMajor>> &cholSigInv,
//                                    const Eigen::VectorXd XpsiY, const Eigen::SparseMatrix<double> Xpsi,
//                                    const int Ns, const Eigen::VectorXd y,
//                                    const double yy, const List spde, double tol) {
//   clock_t time_start, time_setup, time_QK, time_mu, time_sigma2, time_kappa_phi;
//   Rcout << "Starting fixed-point updates" << std::endl;
//   time_start = clock();
//   // Bring in the spde matrices
//   Eigen::SparseMatrix<double> Cmat     = Eigen::SparseMatrix<double> (spde["Cmat"]);
//   Eigen::SparseMatrix<double> Gmat     = Eigen::SparseMatrix<double> (spde["Gmat"]);
//   Eigen::SparseMatrix<double> GtCinvG     = Eigen::SparseMatrix<double> (spde["GtCinvG"]);
//   // Grab metadata
//   int K = (theta.size() - 1) / 2;
//   int sig2_ind = theta.size() - 1;
//   int nKs = A.rows();
//   int ySize = y.size();
//   int n_spde = Cmat.rows();
//   double n_sess = nKs / (n_spde * K);
//   // int Ns = Vh.cols();
//   Eigen::MatrixXd Vh = makeV(nKs,Ns);
//   // Rcout << "dim(A)" << A.rows() << " x " << A.cols() << ", dim(Vh) = " << Vh.rows() << " x " << Vh.cols() << std::endl;
//   Eigen::MatrixXd Avh = A * Vh;
//   // Initialize objects
//   Eigen::SparseMatrix<double> AdivS2(nKs,nKs), Sig_inv(nKs,nKs), Qk(n_spde, n_spde);
//   Eigen::VectorXd theta_new = theta;
//   Eigen::VectorXd muKns(n_spde), Cmu(n_spde), Gmu(n_spde), diagPCVkn(Ns);
//   Eigen::VectorXd diagPGVkn(Ns), GCGmu(n_spde), diagPGCGVkn(Ns);
//   Eigen::MatrixXd Pkn(n_spde, Ns), Vkn(n_spde, Ns), CVkn(n_spde, Ns), PCVkn(Ns, Ns);
//   Eigen::MatrixXd GVkn(n_spde, Ns), PGVkn(Ns, Ns), GCGVkn(n_spde, Ns), PGCGVkn(Ns,Ns);
//   double a_star, b_star, muCmu, muGmu, sumDiagPCVkn, sumDiagPGVkn, muGCGmu;
//   double sumDiagPGCGVkn, phi_partA, phi_partB, phi_partC, new_kappa2, phi_new;
//   double phi_denom = 4.0 * M_PI * n_spde * n_sess;
//   int idx_start;
//   time_setup = clock();
//   // Begin update
//   // for(int k = 0; k < K ; k++) {
//   //   makeQt(&Qk, theta(k), spde);
//   //   Qk = Qk / (4.0 * M_PI * theta(k + K));
//   //   for(int ns = 0; ns < n_sess; ns++) {
//   //     int start_i = k * n_spde + ns * K * n_spde;
//   //     setSparseBlock_update(&QK, start_i, start_i, Qk);
//   //   }
//   // }
//   Rcpp::List Q_list(K * n_sess);
//   int list_idx;
//   for(int ns=0;ns<n_sess;ns++) {
//     for(int k = 0; k < K ; k++) {
//       list_idx = ns*K + k;
//       makeQt(&Qk, theta(k), spde);
//       Qk = Qk / (4.0 * M_PI * theta(k + K));
//       Q_list(list_idx) =  Qk;
//     }
//   }
//   QK = sparseBdiag(Q_list);
//   time_QK = clock();
//   // setSparseCol_update(&QK,Q_list);
//
//   // for(int k = 0; k < K ; k++) {
//   //   makeQt(&Qk, theta(k), spde);
//   //   Qk = Qk / (4.0 * M_PI * theta(k + K));
//   //   for(int ns = 0; ns < n_sess; ns++) {
//   //     int start_i = k * n_spde + ns * K * n_spde;
//   //     setSparseCol_update(&QK, start_i, Qk);
//   //   }
//   // }
//   AdivS2 = A / theta[sig2_ind];
//   Sig_inv = QK + AdivS2;
//   cholSigInv.factorize(Sig_inv);
//   Eigen::VectorXd m = XpsiY / theta(sig2_ind);
//   Eigen::VectorXd mu = cholSigInv.solve(m);
//   time_mu = clock();
//   // Rcout << "First 6 values of mu: " << mu.segment(0,6).transpose() << std::endl;
//   // Solve for sigma_2
//   Eigen::VectorXd XpsiMu = Xpsi * mu;
//   Eigen::MatrixXd P = cholSigInv.solve(Vh);
//   Eigen::MatrixXd PaVh = P.transpose() * Avh;
//   Eigen::VectorXd diagPaVh = PaVh.diagonal();
//   double TrSigA = diagPaVh.sum() / Ns;
//   Eigen::VectorXd Amu = A * mu;
//   double muAmu = mu.transpose() * Amu;
//   double TrAEww = muAmu + TrSigA;
//   // Rcout << "TrAEww = " << TrAEww << std::endl;
//   double yXpsiMu = y.transpose() * XpsiMu;
//   theta_new[sig2_ind] = (yy - 2 * yXpsiMu + TrAEww) / ySize;
//   time_sigma2 = clock();
//   // Update kappa2 and phi by task
//   for(int k = 0; k < K; k++) {
//     a_star = 0.0;
//     b_star = 0.0;
//     muCmu = 0.0;
//     muGmu = 0.0;
//     muGCGmu = 0.0;
//     sumDiagPCVkn = 0.0;
//     sumDiagPGVkn = 0.0;
//     sumDiagPGCGVkn = 0.0;
//     for(int ns = 0; ns < n_sess; ns++) {
//       idx_start = k * n_spde + ns * K * n_spde;
//       // idx_stop = idx_start + n_spde;
//       muKns = mu.segment(idx_start,n_spde);
//       // muCmu
//       Cmu = Cmat * muKns;
//       muCmu += muKns.transpose() * Cmu;
//       // muGmu
//       Gmu = Gmat * muKns;
//       muGmu += muKns.transpose() * Gmu;
//       // muGCGmu
//       GCGmu = GtCinvG * muKns;
//       muGCGmu += muKns.transpose() * GCGmu;
//       // Trace approximations w/ Sigma
//       Pkn = P.block(idx_start, 0, n_spde, Ns);
//       Vkn = Vh.block(idx_start, 0, n_spde, Ns);
//       // Trace of C*Sigma
//       CVkn = Cmat * Vkn;
//       PCVkn = Pkn.transpose() * CVkn;
//       diagPCVkn = PCVkn.diagonal();
//       sumDiagPCVkn += diagPCVkn.sum();
//       // Trace of G*Sigma
//       GVkn = Gmat * Vkn;
//       PGVkn = Pkn.transpose() * GVkn;
//       diagPGVkn = PGVkn.diagonal();
//       sumDiagPGVkn += diagPGVkn.sum();
//       // Trace of GCG*Sigma
//       GCGVkn = GtCinvG * Vkn;
//       PGCGVkn = Pkn.transpose() * GCGVkn;
//       diagPGCGVkn = PGCGVkn.diagonal();
//       sumDiagPGCGVkn += diagPGCGVkn.sum();
//     }
//     sumDiagPCVkn = sumDiagPCVkn / Ns;
//     sumDiagPGVkn = sumDiagPGVkn / Ns;
//     sumDiagPGCGVkn = sumDiagPGCGVkn / Ns;
//     // Update kappa2
//     // Rcout << "muCmu = " << muCmu << ", muGCGmu = " << muGCGmu;
//     // Rcout << ", TrCSig = " << sumDiagPCVkn << ", TrGCGSig = " << sumDiagPGCGVkn << std::endl;
//     // Rcout << "head(diagPCV) = " << diagPCVkn.segment(0,6).transpose() << ", head(diagPGCGV) = " << diagPGCGVkn.segment(0,6).transpose() << std::endl;
//     a_star = (muCmu + sumDiagPCVkn) / (4.0 * M_PI * theta[k + K]);
//     b_star = (muGCGmu + sumDiagPGCGVkn) / (4.0 * M_PI * theta[k + K]);
//     // Rcout << "k = " << k << " a_star = " << a_star << " b_star = " << b_star << std::endl;
//     new_kappa2 = kappa2Brent(0., 50., spde, a_star, b_star, n_sess);
//     // Rcout << ", new_kappa2 = " << new_kappa2 << std::endl;
//     theta_new[k] = new_kappa2;
//     // Update phi
//     phi_partA = sumDiagPCVkn + muCmu;
//     phi_partA = phi_partA * new_kappa2;
//     phi_partB = sumDiagPGVkn + muGmu;
//     phi_partB = 2 * phi_partB;
//     phi_partC = sumDiagPGCGVkn + muGCGmu;
//     phi_partC = phi_partC / new_kappa2;
//     double TrQEww = phi_partA + phi_partB + phi_partC;
//     // Rcout << "TrQEww = " << TrQEww << std::endl;
//     phi_new = TrQEww / phi_denom;
//     theta_new[k + K] = phi_new;
//   }
//   time_kappa_phi = clock();
//   // double time_dif_setup, time_dif_QK, time_dif_mu, time_dif_sigma2, time_dif_kappa_phi;
//   // time_dif_setup = (double)(time_setup - time_start)/CLOCKS_PER_SEC;
//   // time_dif_QK = (double)(time_QK - time_setup)/CLOCKS_PER_SEC;
//   // time_dif_mu = (double)(time_mu - time_QK)/CLOCKS_PER_SEC;
//   // time_dif_sigma2 = (double)(time_sigma2 - time_mu)/CLOCKS_PER_SEC;
//   // time_dif_kappa_phi = (double)(time_kappa_phi - time_sigma2)/CLOCKS_PER_SEC;
//   // Rcout << "Setup: " << time_dif_setup << ", QK: " << time_dif_QK << ", mu: " << time_dif_mu << ", sigma2: " << time_dif_sigma2 << ", kappa & phi: " << time_dif_kappa_phi << std::endl;
//   return(theta_new);
// }

SquaremOutput theta_squarem2(Eigen::VectorXd par, const Eigen::SparseMatrix<double> A,
                       Eigen::SparseMatrix<double> QK,
                       SimplicialLLT<Eigen::SparseMatrix<double>> &cholSigInv,
                       // Eigen::PardisoLLT<Eigen::SparseMatrix<double>> &cholSigInv,
                       const Eigen::VectorXd XpsiY, const Eigen::SparseMatrix<double> Xpsi,
                       const int Ns, const Eigen::VectorXd y, const double yy,
                       const List spde, double tol, bool verbose){
  double res,parnorm,kres; // theta_length=par.size();
  Eigen::VectorXd pcpp,p1cpp,p2cpp,pnew,ptmp;
  Eigen::VectorXd q1,q2,sr2,sq2,sv2,srv;
  double sr2_scalar,sq2_scalar,sv2_scalar,srv_scalar,alpha,stepmin,stepmax;
  // double ob_pcpp, ob_p1cpp, ob_p2cpp, ob_ptmp, ob_pnew, rel_llik_pp1, rel_llik_p1p2, rel_llik_tmpNew;
  int iter,feval;
  bool conv,extrap;
  stepmin=SquaremDefault.stepmin0;
  stepmax=SquaremDefault.stepmax0;
  if(verbose){Rcout<<"Squarem-2"<<std::endl;}

  iter=1;pcpp=par;pnew=par;
  feval=0;conv=true;
  // ob_pcpp = emObj(pcpp,A,QK,cholSigInv,XpsiY,Xpsi,Ns,y,spde);

  const long int parvectorlength=pcpp.size();

  while(feval<SquaremDefault.maxiter){
    //Step 1
    extrap = true;
    // try{p1cpp=fixptfn(pcpp);feval++;}
    try{p1cpp=theta_fixpt(pcpp, A, QK, cholSigInv, XpsiY, Xpsi, Ns, y, yy, spde, tol);feval++;}
    // try{p1cpp=theta_fixptPARDISO(pcpp, A, QK, cholSigInv, XpsiY, Xpsi, Ns, y, yy, spde, tol);feval++;}
    catch(...){
      Rcout<<"Error in fixptfn function evaluation";
      return sqobjnull;
    }
    // ob_p1cpp = emObj(p1cpp,A,QK,cholSigInv,XpsiY,Xpsi,Ns,y,spde);
    // rel_llik_pp1 = std::abs(ob_p1cpp - ob_pcpp) / std::abs(ob_pcpp);

    sr2_scalar=0;
    for (int i=0;i<parvectorlength;i++){sr2_scalar+=std::pow(p1cpp[i]-pcpp[i],2.0);}
    // sr2_scalar = std::sqrt(sr2_scalar);
    double pcpp_norm = pcpp.squaredNorm();
    pcpp_norm = std::sqrt(pcpp_norm);
    // if(sr2_scalar / pcpp_norm < tol){break;}
    // if(std::sqrt(sr2_scalar)<tol){break;}
    if(std::sqrt(sr2_scalar) / pcpp_norm <tol){break;}
    // if(rel_llik_pp1<tol){break;}

    //Step 2
    try{p2cpp=theta_fixpt(p1cpp, A, QK, cholSigInv, XpsiY, Xpsi, Ns, y, yy, spde, tol);feval++;}
    // try{p2cpp=theta_fixptPARDISO(p1cpp, A, QK, cholSigInv, XpsiY, Xpsi, Ns, y, yy, spde, tol);feval++;}
    catch(...){
      Rcout<<"Error in fixptfn function evaluation";
      return sqobjnull;
    }
    // ob_p2cpp = emObj(p2cpp,A,QK,cholSigInv,XpsiY,Xpsi,Ns,y,spde);
    // rel_llik_p1p2 = std::abs(ob_p2cpp - ob_p1cpp) / std::abs(ob_p1cpp);
    sq2_scalar=0;
    for (int i=0;i<parvectorlength;i++){sq2_scalar+=std::pow(p2cpp[i]-p1cpp[i],2.0);}
    sq2_scalar=std::sqrt(sq2_scalar);
    double p1cpp_norm = p1cpp.squaredNorm();
    p1cpp_norm = std::sqrt(p1cpp_norm);
    if (sq2_scalar / p1cpp_norm <tol){break;}
    // if (rel_llik_p1p2<tol){break;}
    // if (sq2_scalar<tol){break;}
    res=sq2_scalar;
    // res=rel_llik_p1p2;

    sv2_scalar=0;
    for (int i=0;i<parvectorlength;i++){sv2_scalar+=std::pow(p2cpp[i]-2.0*p1cpp[i]+pcpp[i],2.0);}
    srv_scalar=0;
    for (int i=0;i<parvectorlength;i++){srv_scalar+=(p2cpp[i]-2.0*p1cpp[i]+pcpp[i])*(p1cpp[i]-pcpp[i]);}
    //std::cout<<"sr2,sv2,srv="<<sr2_scalar<<","<<sv2_scalar<<","<<srv_scalar<<std::endl;//debugging

    //Step 3 Proposing new value
    switch (SquaremDefault.method){
    case 1: alpha= -srv_scalar/sv2_scalar;
    case 2: alpha= -sr2_scalar/srv_scalar;
    case 3: alpha= std::sqrt(sr2_scalar/sv2_scalar);
    }

    alpha=std::max(stepmin,std::min(stepmax,alpha));
    //std::cout<<"alpha="<<alpha<<std::endl;//debugging
    for (int i=0;i<parvectorlength;i++){pnew[i]=pcpp[i]+2.0*alpha*(p1cpp[i]-pcpp[i])+alpha*alpha*(p2cpp[i]-2.0*p1cpp[i]+pcpp[i]);}
    // pnew = pcpp + 2.0*alpha*q1 + alpha*alpha*(q2-q1);
    // ob_pnew = emObj(pnew,A,QK,cholSigInv,XpsiY,Xpsi,Ns,y,spde);

    //Step 4 stabilization
    if(std::abs(alpha-1)>0.01){
      try{ptmp=theta_fixpt(pnew, A, QK, cholSigInv, XpsiY, Xpsi, Ns, y, yy, spde, tol);feval++;}
      // try{ptmp=theta_fixptPARDISO(pnew, A, QK, cholSigInv, XpsiY, Xpsi, Ns, y, yy, spde, tol);feval++;}
      catch(...){
        pnew=p2cpp;
        if(alpha==stepmax){
          stepmax=std::max(SquaremDefault.stepmax0,stepmax/SquaremDefault.mstep);
        }
        alpha=1;
        extrap=false;
        if(alpha==stepmax){stepmax=SquaremDefault.mstep*stepmax;}
        if(stepmin<0 && alpha==stepmin){stepmin=SquaremDefault.mstep*stepmin;}
        pcpp=pnew;
        if(verbose){Rcout<<"Residual: "<<res<<"  Extrapolation: "<<extrap<<"  Steplength: "<<alpha<<std::endl;}
        iter++;
        continue;//next round in while loop
      }
      // ob_ptmp = emObj(ptmp,A,QK,cholSigInv,XpsiY,Xpsi,Ns,y,spde);
      res=0;
      for (int i=0;i<parvectorlength;i++){res+=std::pow(ptmp[i]-pnew[i],2.0);}
      res=std::sqrt(res);
      // double pnew_norm = pnew.squaredNorm();
      // pnew_norm = std::sqrt(pnew_norm);
      // res = res/ pnew_norm;
      // rel_llik_tmpNew = std::abs(ob_ptmp - ob_pnew)/ std::abs(ob_pnew);
      parnorm=0;
      for (int i=0;i<parvectorlength;i++){parnorm+=std::pow(p2cpp[i],2.0);}
      parnorm=std::sqrt(parnorm/parvectorlength);
      kres=SquaremDefault.kr*(1+parnorm)+sq2_scalar;
      if(res <= kres){
        pnew=ptmp;
      }else{
        pnew=p2cpp;
        if(alpha==stepmax){stepmax=SquaremDefault.mstep*stepmax;}
        alpha=1;
        extrap=false;
      }
      // res = rel_llik_tmpNew;
    }

    if(alpha==stepmax){stepmax=SquaremDefault.mstep*stepmax;}
    if(stepmin<0 && alpha==stepmin){stepmin=SquaremDefault.mstep*stepmin;}

    pcpp=pnew;
    if(verbose){Rcout<<"Residual: "<<res<<"  Extrapolation: "<<extrap<<"  Steplength: "<<alpha<<std::endl;}
    iter++;
  }

  if (feval >= SquaremDefault.maxiter){conv=false;}

  //assigning values
  sqobj.par=pcpp;
  sqobj.valueobjfn=NAN;
  sqobj.iter=iter;
  sqobj.pfevals=feval;
  sqobj.objfevals=0;
  sqobj.convergence=conv;
  return(sqobj);
}

SquaremOutput theta_squarem2_CG(Eigen::VectorXd par, const Eigen::SparseMatrix<double> A,
                             Eigen::SparseMatrix<double> QK,
                             SimplicialLLT<Eigen::SparseMatrix<double>> &cholSigInv,
                             SimplicialLLT<Eigen::SparseMatrix<double>> &cholQ,
                             // Eigen::PardisoLLT<Eigen::SparseMatrix<double>> &cholSigInv,
                             const Eigen::VectorXd XpsiY, const Eigen::SparseMatrix<double> Xpsi,
                             const int Ns, const Eigen::VectorXd y, const double yy,
                             const Eigen::MatrixXd d2f1,
                             const List spde, double tol, bool verbose){
  double res,parnorm,kres; // theta_length=par.size();
  Eigen::VectorXd pcpp,p1cpp,p2cpp,pnew,ptmp;
  Eigen::VectorXd q1,q2,sr2,sq2,sv2,srv;
  double sr2_scalar,sq2_scalar,sv2_scalar,srv_scalar,alpha,stepmin,stepmax;
  // double ob_pcpp, ob_p1cpp, ob_p2cpp, ob_ptmp, ob_pnew, rel_llik_pp1, rel_llik_p1p2, rel_llik_tmpNew;
  int iter,feval;
  bool conv,extrap;
  stepmin=SquaremDefault.stepmin0;
  stepmax=SquaremDefault.stepmax0;
  if(verbose){Rcout<<"Squarem-2"<<std::endl;}

  iter=1;pcpp=par;pnew=par;
  feval=0;conv=true;
  // ob_pcpp = emObj(pcpp,A,QK,cholSigInv,XpsiY,Xpsi,Ns,y,spde);

  const long int parvectorlength=pcpp.size();

  while(feval<SquaremDefault.maxiter){
    //Step 1
    extrap = true;
    // try{p1cpp=fixptfn(pcpp);feval++;}
    try{p1cpp=theta_fixpt_CG(pcpp, A, QK, cholSigInv, cholQ, XpsiY, Xpsi, Ns, y, d2f1, yy, spde, tol);feval++;}
    // try{p1cpp=theta_fixptPARDISO(pcpp, A, QK, cholSigInv, XpsiY, Xpsi, Ns, y, yy, spde, tol);feval++;}
    catch(...){
      Rcout<<"Error in fixptfn function evaluation";
      return sqobjnull;
    }
    // ob_p1cpp = emObj(p1cpp,A,QK,cholSigInv,XpsiY,Xpsi,Ns,y,spde);
    // rel_llik_pp1 = std::abs(ob_p1cpp - ob_pcpp) / std::abs(ob_pcpp);

    sr2_scalar=0;
    for (int i=0;i<parvectorlength;i++){sr2_scalar+=std::pow(p1cpp[i]-pcpp[i],2.0);}
    // sr2_scalar = std::sqrt(sr2_scalar);
    double pcpp_norm = pcpp.squaredNorm();
    pcpp_norm = std::sqrt(pcpp_norm);
    // if(sr2_scalar / pcpp_norm < tol){break;}
    // if(std::sqrt(sr2_scalar)<tol){break;}
    if(std::sqrt(sr2_scalar) / pcpp_norm <tol){break;}
    // if(rel_llik_pp1<tol){break;}

    //Step 2
    try{p2cpp=theta_fixpt_CG(p1cpp, A, QK, cholSigInv, cholQ, XpsiY, Xpsi, Ns, y, d2f1, yy, spde, tol);feval++;}
    // try{p2cpp=theta_fixptPARDISO(p1cpp, A, QK, cholSigInv, XpsiY, Xpsi, Ns, y, yy, spde, tol);feval++;}
    catch(...){
      Rcout<<"Error in fixptfn function evaluation";
      return sqobjnull;
    }
    // ob_p2cpp = emObj(p2cpp,A,QK,cholSigInv,XpsiY,Xpsi,Ns,y,spde);
    // rel_llik_p1p2 = std::abs(ob_p2cpp - ob_p1cpp) / std::abs(ob_p1cpp);
    sq2_scalar=0;
    for (int i=0;i<parvectorlength;i++){sq2_scalar+=std::pow(p2cpp[i]-p1cpp[i],2.0);}
    sq2_scalar=std::sqrt(sq2_scalar);
    double p1cpp_norm = p1cpp.squaredNorm();
    p1cpp_norm = std::sqrt(p1cpp_norm);
    if (sq2_scalar / p1cpp_norm <tol){break;}
    // if (rel_llik_p1p2<tol){break;}
    // if (sq2_scalar<tol){break;}
    res=sq2_scalar;
    // res=rel_llik_p1p2;

    sv2_scalar=0;
    for (int i=0;i<parvectorlength;i++){sv2_scalar+=std::pow(p2cpp[i]-2.0*p1cpp[i]+pcpp[i],2.0);}
    srv_scalar=0;
    for (int i=0;i<parvectorlength;i++){srv_scalar+=(p2cpp[i]-2.0*p1cpp[i]+pcpp[i])*(p1cpp[i]-pcpp[i]);}
    //std::cout<<"sr2,sv2,srv="<<sr2_scalar<<","<<sv2_scalar<<","<<srv_scalar<<std::endl;//debugging

    //Step 3 Proposing new value
    switch (SquaremDefault.method){
    case 1: alpha= -srv_scalar/sv2_scalar;
    case 2: alpha= -sr2_scalar/srv_scalar;
    case 3: alpha= std::sqrt(sr2_scalar/sv2_scalar);
    }

    alpha=std::max(stepmin,std::min(stepmax,alpha));
    //std::cout<<"alpha="<<alpha<<std::endl;//debugging
    for (int i=0;i<parvectorlength;i++){pnew[i]=pcpp[i]+2.0*alpha*(p1cpp[i]-pcpp[i])+alpha*alpha*(p2cpp[i]-2.0*p1cpp[i]+pcpp[i]);}
    // pnew = pcpp + 2.0*alpha*q1 + alpha*alpha*(q2-q1);
    // ob_pnew = emObj(pnew,A,QK,cholSigInv,XpsiY,Xpsi,Ns,y,spde);

    //Step 4 stabilization
    if(std::abs(alpha-1)>0.01){
      try{ptmp=theta_fixpt_CG(pnew, A, QK, cholSigInv, cholQ, XpsiY, Xpsi, Ns, y, d2f1, yy, spde, tol);feval++;}
      // try{ptmp=theta_fixptPARDISO(pnew, A, QK, cholSigInv, XpsiY, Xpsi, Ns, y, yy, spde, tol);feval++;}
      catch(...){
        pnew=p2cpp;
        if(alpha==stepmax){
          stepmax=std::max(SquaremDefault.stepmax0,stepmax/SquaremDefault.mstep);
        }
        alpha=1;
        extrap=false;
        if(alpha==stepmax){stepmax=SquaremDefault.mstep*stepmax;}
        if(stepmin<0 && alpha==stepmin){stepmin=SquaremDefault.mstep*stepmin;}
        pcpp=pnew;
        if(verbose){Rcout<<"Residual: "<<res<<"  Extrapolation: "<<extrap<<"  Steplength: "<<alpha<<std::endl;}
        iter++;
        continue;//next round in while loop
      }
      // ob_ptmp = emObj(ptmp,A,QK,cholSigInv,XpsiY,Xpsi,Ns,y,spde);
      res=0;
      for (int i=0;i<parvectorlength;i++){res+=std::pow(ptmp[i]-pnew[i],2.0);}
      res=std::sqrt(res);
      // double pnew_norm = pnew.squaredNorm();
      // pnew_norm = std::sqrt(pnew_norm);
      // res = res/ pnew_norm;
      // rel_llik_tmpNew = std::abs(ob_ptmp - ob_pnew)/ std::abs(ob_pnew);
      parnorm=0;
      for (int i=0;i<parvectorlength;i++){parnorm+=std::pow(p2cpp[i],2.0);}
      parnorm=std::sqrt(parnorm/parvectorlength);
      kres=SquaremDefault.kr*(1+parnorm)+sq2_scalar;
      if(res <= kres){
        pnew=ptmp;
      }else{
        pnew=p2cpp;
        if(alpha==stepmax){stepmax=SquaremDefault.mstep*stepmax;}
        alpha=1;
        extrap=false;
      }
      // res = rel_llik_tmpNew;
    }

    if(alpha==stepmax){stepmax=SquaremDefault.mstep*stepmax;}
    if(stepmin<0 && alpha==stepmin){stepmin=SquaremDefault.mstep*stepmin;}

    pcpp=pnew;
    if(verbose){Rcout<<"Residual: "<<res<<"  Extrapolation: "<<extrap<<"  Steplength: "<<alpha<<std::endl;}
    iter++;
  }

  if (feval >= SquaremDefault.maxiter){conv=false;}

  //assigning values
  sqobj.par=pcpp;
  sqobj.valueobjfn=NAN;
  sqobj.iter=iter;
  sqobj.pfevals=feval;
  sqobj.objfevals=0;
  sqobj.convergence=conv;
  return(sqobj);
}

//' Perform the EM algorithm of the Bayesian GLM fitting
//'
//' @param theta the vector of initial values for theta
//' @param spde a list containing the sparse matrix elements Cmat, Gmat, and GtCinvG
//' @param y the vector of response values
//' @param X the sparse matrix of the data values
//' @param QK a sparse matrix of the prior precision found using the initial values of the hyperparameters
//' @param Psi a sparse matrix representation of the basis function mapping the data locations to the mesh vertices
//' @param A a precomputed matrix crossprod(X%*%Psi)
//' @param Ns the number of columns for the random matrix used in the Hutchinson estimator
//' @param tol a value for the tolerance used for a stopping rule (compared to
//'   the squared norm of the differences between \code{theta(s)} and \code{theta(s-1)})
//' @param CG (logical) Should conjugate gradient methods be used?
//' @param verbose (logical) Should intermediate output be displayed?
//' @export
// [[Rcpp::export(rng = false)]]
Rcpp::List findTheta(Eigen::VectorXd theta, List spde, Eigen::VectorXd y,
                     Eigen::SparseMatrix<double> X, Eigen::SparseMatrix<double> QK,
                     Eigen::SparseMatrix<double> Psi, Eigen::SparseMatrix<double> A,
                     int Ns, double tol, bool CG = false, bool verbose = false) {
  // Bring in the spde matrices
  Eigen::SparseMatrix<double> Cmat     = Eigen::SparseMatrix<double> (spde["Cmat"]);
  Eigen::SparseMatrix<double> Gmat     = Eigen::SparseMatrix<double> (spde["Gmat"]);
  Eigen::SparseMatrix<double> GtCinvG     = Eigen::SparseMatrix<double> (spde["GtCinvG"]);
  int n_spde = Cmat.rows();
  int K = theta.size();
  K = (K - 1) / 2;
  int sig2_ind = 2*K;
  Eigen::SparseMatrix<double> AdivS2 = A / theta[sig2_ind];
  Eigen::SparseMatrix<double> Sig_inv = QK + AdivS2;
  SimplicialLLT<Eigen::SparseMatrix<double>> cholSigInv;
  // Eigen::PardisoLLT<Eigen::SparseMatrix<double,Eigen::RowMajor>> cholSigInv;
  cholSigInv.analyzePattern(Sig_inv);
  cholSigInv.factorize(Sig_inv);
  SimplicialLLT<Eigen::SparseMatrix<double>> cholQ; //  for CG
  if(CG) {
    cholQ.analyzePattern(QK); //  for CG
    cholQ.factorize(QK); // for CG
  }
  if(verbose) {Rcout << "Initial theta: " << theta.transpose() << std::endl;}
  // Initialize everything
  Eigen::SparseMatrix<double> Xpsi = X * Psi;
  Eigen::SparseMatrix<double> Qk(n_spde, n_spde);
  Eigen::VectorXd XpsiY = Xpsi.transpose() * y;
  // Eigen::MatrixXd Avh = A * Vh;
  double yy = y.transpose() * y;
  // Regular fixed point updates
  // Eigen::VectorXd theta_new;
  // Eigen::VectorXd thetaDiff(2 * K + 1);
  // int Step = 0;
  // double eps = tol + 1;
  // // while(eps > tol | Step < 5) {
  // while(Step < 1) {
  //   theta_new = theta_fixpt(theta, A, QK, cholSigInv, XpsiY, Xpsi, Vh, Avh, y, yy, spde, tol);
  //   Step += 1;
  //   Rcout << "Step " << Step << " theta: " << theta_new.transpose() << std::endl;
  //   thetaDiff = theta_new - theta;
  //   // This uses the percent
  //   // thetaDiff = thetaDiff.array() / theta.array();
  //   // eps = thetaDiff.maxCoeff();
  //   // Rcout << "eps = " << eps << std::endl;
  //   // The following uses the sqrt of the squared norm to determine whether to stop
  //   eps = thetaDiff.squaredNorm();
  //   eps = std::sqrt(eps);
  //   theta = theta_new;
  // }
  // Using SQUAREM
  SquaremOutput SQ_result;
  SquaremDefault.tol = tol;
  if(CG) {
    int nKs = A.rows();
    int n_sess = nKs / (n_spde * K);
    Rcout << "Precalculating the approximate second derivative for conjugate gradient...";
    Eigen::MatrixXd d2f1_k = d2f1_kappa(spde, 50, 200, 2.5, n_sess);
    Rcout << "DONE!" << std::endl;
    SQ_result = theta_squarem2_CG(theta, A, QK, cholSigInv, cholQ, XpsiY, Xpsi, Ns, y, yy, d2f1_k, spde, tol, verbose);
  }
  if(!CG){
    SQ_result = theta_squarem2(theta, A, QK, cholSigInv, XpsiY, Xpsi, Ns, y, yy, spde, tol, verbose);
  }
  theta= SQ_result.par;
  // Bring results together for output
  if(verbose) {Rcout << "Final theta: " << theta.transpose() << std::endl;}
  AdivS2 = A / theta[sig2_ind];
  Sig_inv = QK + AdivS2;
  cholSigInv.factorize(Sig_inv);
  Eigen::VectorXd m = XpsiY / theta(sig2_ind);
  Eigen::VectorXd mu = cholSigInv.solve(m);
  List out = List::create(Named("theta_new") = theta,
                          Named("kappa2_new") = theta.segment(0,K),
                          Named("phi_new") = theta.segment(K,K),
                          Named("sigma2_new") = theta(2*K),
                          Named("mu") = mu);
  return out;
}


