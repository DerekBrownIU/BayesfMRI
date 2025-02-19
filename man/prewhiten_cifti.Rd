% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/prewhitening.R
\name{prewhiten_cifti}
\alias{prewhiten_cifti}
\title{Prewhiten cifti session data}
\usage{
prewhiten_cifti(
  data,
  mask = NULL,
  scale_BOLD = TRUE,
  scale_design = TRUE,
  ar_order = 6,
  ar_smooth = 5,
  cifti_data,
  brainstructure,
  num.threads = NULL
)
}
\arguments{
\item{data}{List of sessions (see \code{is.session} and \code{is.session_pw})}

\item{mask}{(Optional) A length \eqn{V} logical vector indicating if each
vertex is to be included.}

\item{scale_BOLD}{(logical) Should the BOLD response be scaled? (Default is TRUE)}

\item{scale_design}{(logical) Should the design matrix be scaled? (Default is TRUE)}

\item{ar_order}{Order of the AR used to prewhiten the data at each location}

\item{ar_smooth}{FWHM parameter for smoothing. Remember that
\eqn{\sigma = \frac{FWHM}{2*sqrt(2*log(2)}}. Set to \code{0} or \code{NULL}
to not do any smoothing. Default: \code{5}.}

\item{cifti_data}{A \code{xifti} object used to map the AR coefficient
estimates onto the surface mesh for smoothing.}

\item{brainstructure}{'left' or 'right' or 'subcortical'}

\item{num.threads}{(scalar) The number of threads to use in parallelizing the
prewhitening}
}
\value{
The prewhitened data (in a list), the smoothed, averaged AR
coefficient estimates used in the prewhitening, the smoothed, average
residual variance after prewhitening, and the value given for \code{ar_order}.
}
\description{
Prewhiten cifti session data
}
