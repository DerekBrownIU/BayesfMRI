#' Compute inverse covariance matrix for AR process (up to a constant scaling factor)
#'
#' @param ar vector of p AR parameters
#' @param ntime number of time points in timeseries
#'
#' @return inverse covariance matrix for AR process (up to a constant scaling factor)
#' @importFrom Matrix diag
#' @export
getInvCovAR <- function(ar, ntime){
  Inv0 <- diag(ntime)
  incr0 <- matrix(0, nrow=ntime, ncol=ntime)
  offs <- row(Inv0) - col(Inv0) #identifies each off-diagonal
  p <- length(ar)
  for(k in 1:p){
    incr <- incr0 #matrix of zeros
    incr[offs==k] <- -1*ar[k]
    Inv0 <- Inv0 + incr
  }
  Inv <- Inv0 %*% t(Inv0)
  return(Inv)
}


#' Compute square root of a symmetric matrix (such as inverse covariance mat) using SVD
#'
#' @param Inv Symmetric matrix
#'
#' @return Matrix square root of \code{Inv}
#' @importFrom Matrix diag
#' @keywords internal
getSqrtInv <- function(Inv){
  #
  # Cov = UD^2U', then Inv = UD^(-2)U', and Inv_sqrt = UD^(-1)U'
  ei <- eigen(Inv)
  d2inv <- ei$values #diag(D^(-2))
  if(sum(d2inv<0)>0) print('negative eigenvalues')
  sqrtInv <- ei$vectors %*% diag(sqrt(d2inv)) %*% t(ei$vectors)
  return(sqrtInv)
}

#' Prewhitening for a single time series
#'
#' @param AR_coeffs A vector of AR(p) coefficients for an order p autoregressive
#'   prewhitening
#' @param ntime (scalar) The length of the time series to be prewhitened
#' @param AR_var (scalar) The variance of the time series to include a
#'   normalization term to make the prewhitened series have variance equal to 1.
#'   If none is provided, then the default is 1, resulting in no normalization.
#'
#' @return A sparse matrix with the class \code{dsCMatrix} with \code{ntime}
#'   rows and columns that can be postmultiplied by the original time series
#'   in order to perform prewhitening.
#' @importFrom Matrix sparseMatrix bandSparse
#' @export
#'
#' @examples
#' \dontrun{
#' x <- arima.sim(list(ar = c(.3,.2,.1)), n = 100)
#' acf(x)
#' ar_model <- ar.yw(x,order.max = 6, aic = F)
#' pw_mat <- prewhiten.v(AR_coeffs = ar_model$ar,
#'                       ntime = length(x),
#'                       AR_var = ar_model$var.pred)
#' y <- as.vector(pw_mat %*% as.matrix(x))
#' acf(y)
#' plot(x, type = 'l')
#' lines(y, col = 'red')
#' }
prewhiten.v <- function(AR_coeffs, ntime, AR_var = 1) {
  off_diags <- row(diag(ntime)) - col(diag(ntime))
  ar_order <- length(AR_coeffs)
  if(is.na(AR_coeffs[1])) {
    return(Matrix::sparseMatrix(i=NULL, j=NULL, dims=c(ntime, ntime)))
  } else {
    Inv.v <- getInvCovAR(AR_coeffs, ntime) #inverse correlation (sort of)
    Dinv.v <- diag(rep(1/sqrt(AR_var), ntime)) #inverse diagonal SD matrix
    Inv.v <- Dinv.v %*% Inv.v %*% Dinv.v #inverse covariance
    sqrtInv.v <- getSqrtInv(Inv.v) #find V^(1) and take sqrt
    #after the (p+1)th off-diagonal, values of sqrtInv.v very close to zero
    diags <- list(); length(diags) <- ar_order+2
    for(k in 0:(ar_order+1)){
      diags[[k+1]] <- sqrtInv.v[off_diags==k]
    }
    matband <- Matrix::bandSparse(ntime, k=0:(ar_order+1), symmetric=TRUE, diagonals=diags)
    return(matband)
  }
}

#' Prewhiten cifti session data
#'
#' @param data List of sessions (see \code{is.session} and \code{is.session_pw})
#' @param mask (Optional) A length \eqn{V} logical vector indicating if each
#'  vertex is to be included.
#' @param scale_BOLD (logical) Should the BOLD response be scaled? (Default is TRUE)
#' @param scale_design (logical) Should the design matrix be scaled? (Default is TRUE)
#' @param ar_order Order of the AR used to prewhiten the data at each location
#' @param ar_smooth FWHM parameter for smoothing. Remember that
#'  \eqn{\sigma = \frac{FWHM}{2*sqrt(2*log(2)}}. Set to \code{0} or \code{NULL}
#'  to not do any smoothing. Default: \code{5}.
#' @param cifti_data A \code{xifti} object used to map the AR coefficient
#'   estimates onto the surface mesh for smoothing.
#' @param brainstructure 'left' or 'right' or 'subcortical'
#' @param num.threads (scalar) The number of threads to use in parallelizing the
#'   prewhitening
#' @importFrom Matrix bandSparse bdiag
#' @importFrom ciftiTools smooth_cifti
#' @importFrom stats ar.yw
#' @importFrom parallel detectCores makeCluster clusterMap stopCluster
#'
#' @return The prewhitened data (in a list), the smoothed, averaged AR
#'   coefficient estimates used in the prewhitening, the smoothed, average
#'   residual variance after prewhitening, and the value given for \code{ar_order}.
#' @export
prewhiten_cifti <- function(data,
                            mask = NULL,
                            scale_BOLD = TRUE,
                            scale_design = TRUE,
                            ar_order = 6,
                            ar_smooth = 5,
                            cifti_data,
                            brainstructure,
                            num.threads = NULL) {
  #check that all elements of the data list are valid sessions and have the same number of locations and tasks
  session_names <- names(data)
  n_sess <- length(session_names)

  if(!is.list(data)) stop('I expect data to be a list, but it is not')
  data_classes <- sapply(data, 'class')
  if(! all.equal(unique(data_classes),'list')) stop('I expect data to be a list of lists (sessions), but it is not')

  if(missing(brainstructure)) stop('Please provide valid brainstructure argument.')
  if(!(brainstructure %in% c('left','right','subcortical'))) stop('Please provide valid brainstructure argument.')

  ######################

  #ID any zero-variance voxels and remove from analysis
  zero_var <- sapply(data, function(x){
    x$BOLD[is.na(x$BOLD)] <- 0 #to detect medial wall locations coded as NA
    x$BOLD[is.nan(x$BOLD)] <- 0 #to detect medial wall locations coded as NaN
    vars <- matrixStats::colVars(x$BOLD)
    return(vars < 1e-6)
  })
  zero_var <- (rowSums(zero_var) > 0) #check whether any vertices have zero variance in any session

  #remove zero var locations from mask
  if(sum(zero_var) > 0){
    if(is.null(mask)) num_flat <- sum(zero_var) else num_flat <- sum(zero_var[mask==1])
    #if(num_flat > 1) warning(paste0('I detected ', num_flat, ' vertices that are flat (zero variance), NA or NaN in at least one session. Removing these from analysis. See mask returned with function output.'))
    #if(num_flat == 1) warning(paste0('I detected 1 vertex that is flat (zero variance), NA or NaN in at least one session. Removing it from analysis. See mask returned with function output.'))
    mask_orig <- mask
    if(!is.null(mask)) mask[zero_var==TRUE] <- 0
    if(is.null(mask)) mask <- !zero_var
  } else {
    mask_orig <- NULL
  }

  #apply mask to data
  if(is.null(mask)) mask_use <- rep(TRUE, ncol(data[[1]]$BOLD)) else mask_use <- as.logical(mask)
  V_all <- length(mask_use)
  V <- sum(mask_use)
  for(s in 1:n_sess){
    data[[s]]$BOLD <- data[[s]]$BOLD[,mask_use]
  }

  K <- ncol(data[[1]]$design) #number of tasks
  ntime <- nrow(data[[1]]$BOLD) # Number of time steps
  for(s in 1:n_sess){
    if(! is.session(data[[s]])) stop('I expect each element of data to be a session object, but at least one is not (see `is.session`).')
    if(ncol(data[[s]]$BOLD) != V) stop('All sessions must have the same number of data locations, but they do not.')
    if(ncol(data[[s]]$design) != K) stop('All sessions must have the same number of tasks (columns of the design matrix), but they do not.')
  }

  GLM_result <- vector('list', length=n_sess)
  names(GLM_result) <- session_names
  AR_coeffs <- array(dim = c(V,ar_order,n_sess))
  AR_resid_var <- array(dim = c(V,n_sess))
  for(s in 1:n_sess){
    #scale data to represent % signal change (or just center to remove baseline if scale=FALSE)
    BOLD_s <- data[[s]]$BOLD
    BOLD_s <- scale_timeseries(t(BOLD_s), scale=scale_BOLD, transpose = FALSE)
    if(scale_design) {
      design_s <- scale_design_mat(data[[s]]$design) #center design matrix and scale
    } else {
      design_s <- scale(data[[s]]$design, scale=FALSE) #center design matrix to eliminate baseline
    }

    #regress nuisance parameters from BOLD data and design matrix
    if('nuisance' %in% names(data[[s]])){
      design_s <- data[[s]]$design
      nuisance_s <- data[[s]]$nuisance
      y_reg <- nuisance_regress(BOLD_s, nuisance_s)
      X_reg <- nuisance_regress(design_s, nuisance_s)
    } else {
      y_reg <- BOLD_s
      X_reg <- data[[s]]$design
    }
    XTX_inv <- try(solve(crossprod(X_reg)))
    if("try-error" %in% class(XTX_inv)) {
      stop("There is some numerical instability in your design matrix
                    (due to very large or very small values). Scaling the design
                    matrix is suggested.")
    } else {
      # GLM_result[[s]] <- t(XTX_inv %*% t(X_reg) %*% y_reg)
      betas <- t(XTX_inv %*% t(X_reg) %*% y_reg)
      resids <- y_reg - tcrossprod(X_reg,betas)
      for(v in 1:V) {
        if(is.na(resids[1,v])) next
        ar_v <- ar.yw(resids[,v],aic = FALSE,order.max = ar_order)
        # aic_order <- ar.yw(resids[,v])$order # This should be the order
        # of the time series if the AIC is used to select the best order
        AR_coeffs[v,,s] <- ar_v$ar # The AR parameter estimates
        AR_resid_var[v,s] <- ar_v$var.pred # Resulting variance
      }
    }
    data[[s]]$BOLD <- y_reg
    data[[s]]$design <- X_reg
  }

  avg_AR <- apply(AR_coeffs,1:2, mean)
  avg_var <- apply(as.matrix(AR_resid_var),1,mean)

  if (is.null(ar_smooth)) { ar_smooth <- 0 }
  if((ar_smooth != 0) & !is.null(cifti_data)) {
    cat("Smoothing AR coefficients and residual variance...")
    #set up template xifti object with only one brainstructure
    avg_xifti <- cifti_data
    if(brainstructure %in% c("left","right")) {
      if(brainstructure=='left') mwall_mask <- avg_xifti$meta$cortex$medial_wall_mask$left
      if(brainstructure=='right') mwall_mask <- avg_xifti$meta$cortex$medial_wall_mask$right
      mask_tmp <- as.logical(mask_use[mwall_mask==TRUE])
      #smooth AR coefficients
      avg_xifti$data[[paste0("cortex_",brainstructure)]] <- matrix(NA, nrow=sum(mwall_mask), ncol=ar_order)
      avg_xifti$data[[paste0("cortex_",brainstructure)]][mask_tmp,] <- avg_AR
      smooth_avg_xifti <- smooth_cifti(avg_xifti, surf_FWHM = ar_smooth)
      avg_AR <- smooth_avg_xifti$data[[paste0("cortex_",brainstructure)]][mask_tmp,]
      #smooth variance
      avg_xifti$data[[paste0("cortex_",brainstructure)]] <- matrix(NA, nrow=sum(mwall_mask), ncol=1)
      avg_xifti$data[[paste0("cortex_",brainstructure)]][as.logical(mask_tmp),] <- avg_var
      smooth_var_xifti <- smooth_cifti(avg_xifti, surf_FWHM = ar_smooth)
      avg_var <- smooth_var_xifti$data[[paste0("cortex_",brainstructure)]][mask_tmp,,drop=FALSE]
    }
    if(brainstructure == 'subcortical') {
      sub_mask <- avg_xifti$meta$subcort$mask
      if(!is.null(mask)){
        sub_mask[sub_mask] <- mask
        avg_xifti$meta$subcort$mask <- sub_mask
        avg_xifti$meta$subcort$labels <- avg_xifti$meta$subcort$labels[mask]
      }
      #smooth AR coefficients
      avg_xifti$data$subcort <- as.matrix(avg_AR)
      avg_xifti$meta$cifti$names <- avg_xifti$meta$cifti$names[seq_len(ncol(as.matrix(avg_AR)))]
      smooth_avg_xifti <- smooth_cifti(avg_xifti, vol_FWHM = ar_smooth)
      avg_AR <- smooth_avg_xifti$data$subcort
      #smooth variance
      avg_xifti$data$subcort <- as.matrix(avg_var)
      avg_xifti$meta$cifti$names <- avg_xifti$meta$cifti$names[seq_len(ncol(as.matrix(avg_var)))]
      smooth_var_xifti <- smooth_cifti(avg_xifti, vol_FWHM = ar_smooth)
      avg_var <- smooth_var_xifti$data$subcort
    }
    cat("done!\n")
  }
  # Create the sparse pre-whitening matrix
  cat("Prewhitening... ")
  if(is.null(num.threads) | num.threads < 2) {
    # Initialize the block diagonal covariance matrix
    template_pw <- Matrix::bandSparse(n = ntime,
                                      k = 0:(ar_order + 1),
                                      symmetric = TRUE)
    template_pw_list <- rep(list(template_pw),V)
    rows.rm <- numeric()
    for(v in 1:V) {
      if(v %% 100 == 0) cat("\n Location",v,"of",V,"")
      # template_pw_list[[v]] <- prewhiten.v(AR_coeffs = avg_AR[v,],
      #                                      ntime = ntime,
      #                                      AR_var = avg_var[v])
      template_pw_list[[v]] <- getSqrtInvCpp(AR_coeffs = avg_AR[v,],
                                           nTime = ntime,
                                           avg_var = avg_var[v])
    }
  } else {
    if (!requireNamespace("parallel", quietly = TRUE)) {
      stop("Prewhitening in parallel requires the `parallel` package. Please install it.", call. = FALSE)
    }
    max_threads <- max(parallel::detectCores(), 25)
    num_threads <- min(max_threads,num.threads)
    cl <- parallel::makeCluster(num_threads)
    template_pw_list <-
      parallel::clusterMap(
        cl,
        # prewhiten.v,
        getSqrtInvCpp,
        AR_coeffs = split(avg_AR, row(avg_AR)),
        # ntime = ntime,
        nTime = ntime,
        # AR_var = avg_var,
        avg_var = avg_var,
        SIMPLIFY = FALSE
      )
    parallel::stopCluster(cl)
  }
  cat("done!\n")

  sqrtInv_all <- Matrix::bdiag(template_pw_list)
  pw_data <- sapply(data,function(data_s) {
    bold_out <- matrix(NA,ntime, V_all)
    pw_BOLD <- as.vector(sqrtInv_all %*% c(data_s$BOLD))
    bold_out[,mask_use] <- pw_BOLD
    all_design <- Matrix::bdiag(rep(list(data_s$design),V))
    pw_design <- sqrtInv_all %*% all_design
    return(list(BOLD = bold_out, design = pw_design))
  }, simplify = F)

  return(list(data = pw_data,
              AR_coeffs = avg_AR,
              AR_var = avg_var,
              ar_order = ar_order,
              mask = mask,
              mask_orig = mask_orig))
}
