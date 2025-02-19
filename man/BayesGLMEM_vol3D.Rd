% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/EM_functions.R
\name{BayesGLMEM_vol3D}
\alias{BayesGLMEM_vol3D}
\title{BayesGLM for 3D volume}
\usage{
BayesGLMEM_vol3D(
  data,
  beta_names = NULL,
  locations,
  labels,
  use_SQUAREM = TRUE,
  ar_order = 0,
  ar_smooth = 0,
  cifti_data = NULL,
  tol = NULL,
  groups_df = NULL,
  scale_BOLD = TRUE,
  scale_design = TRUE,
  outfile = NULL,
  num.threads = 6,
  verbose = FALSE,
  avg_sessions = TRUE
)
}
\arguments{
\item{data}{A list of sessions, where each session is a list with elements
BOLD, design and nuisance.  See \code{?create.session} and \code{?is.session} for more details.
List element names represent session names.}

\item{beta_names}{(Optional) Names of tasks represented in design matrix}

\item{locations}{Vx3 matrix of x,y,z coordinates of each voxel}

\item{labels}{Vector of length V of region labels}

\item{use_SQUAREM}{(logical) Should the SQUAREM package be used to speed up
convergence?}

\item{ar_order}{(numeric) Controls prewhitening. If greater than zero, this
should be a number indicating the order of the autoregressive model to use
for prewhitening. If zero, do not prewhiten. Default: \code{0}.}

\item{ar_smooth}{FWHM parameter for smoothing. Remember that
\eqn{\sigma = \frac{FWHM}{2*sqrt(2*log(2)}}. Set to \code{0} or \code{NULL}
to not do any smoothing. Default: \code{0}.}

\item{cifti_data}{A \code{xifti} object with data and metadata pertaining to
the subcortex. Used to smooth AR coefficients during prewhitening.}

\item{tol}{If use_SQUAREM == TRUE, an absolute change limit for
when the EM algorithm should be stopped (Default = 1e-3). If use_SQUAREM ==
FALSE, a percent change limit for when the EM algorithm should be stopped
(Default = 1). A value of NULL will result in the default value being used.}

\item{groups_df}{Data frame indicating the name and model group of each region.  See Details.}

\item{scale_BOLD}{Scale timeseries data so estimates represent percent signal
change? Default: \code{TRUE}. If \code{FALSE}, the data will just be
centered.}

\item{scale_design}{Scale the design matrix by dividing each column by its
maximum and then subtracting the mean? Default: \code{TRUE}.}

\item{outfile}{File name where results will be written (for use by \code{BayesGLM_grp}).}

\item{num.threads}{The maximum number of threads to use in the inla-program
for model estimation. Default: \code{4}.}

\item{verbose}{Should INLA be run in verbose mode? Default: \code{FALSE}.}

\item{avg_sessions}{Average estimates for betas over multiple
sessions? Default: \code{TRUE}.}
}
\value{
A list containing...
}
\description{
Applies spatial Bayesian GLM to task fMRI data for 3D subcortical volumes
}
\details{
The subcortical data is separated into regions, whose sizes range
from approximately 100 voxels to approximately 9000 voxels.  Smaller regions
are grouped together to improve model fit.
The \code{groups_df} argument specifies which regions are grouped together.
This argument should be a data frame with R rows (the number of regions) and
three columns: label, region, and group.
The label column is the numerical identifier of each region; the region
column contains the region names, and the group column contains the model
group assignments (e.g. 1,2,3). Regions to be excluded
from analysis are indicated by NA in the group assignment.
}
