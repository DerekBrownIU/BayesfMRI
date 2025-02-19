% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/BayesGLM2.R
\name{BayesGLM2_vol}
\alias{BayesGLM2_vol}
\title{Group-level Bayesian GLM for subcortical data}
\usage{
BayesGLM2_vol(
  results,
  contrasts = NULL,
  quantiles = NULL,
  excursion_type = NULL,
  gamma = list(0),
  alpha = 0.05,
  nsamp_theta = 50,
  nsamp_beta = 100,
  verbose = TRUE
)
}
\arguments{
\item{results}{A character vector of length M of file names output from the
BayesGLM_cifti function with subcortical results. M is the number of subjects.}

\item{contrasts}{(Optional) A list of vectors, each length \code{M * K * num_sessions}, specifying the contrast(s)
of interest across subjects, where M is the number of subjects and K is the number of tasks.
See Details for more information. Default is to compute the average for each task across subjects.}

\item{quantiles}{(Optional) Vector of posterior quantiles to return in addition to the posterior mean}

\item{excursion_type}{(For inference only) The type of excursion function for the contrast (">", "<", "!="),
or a vector thereof (each element corresponding to one contrast).  If NULL, no inference performed.}

\item{gamma}{(For inference only) List of vectors of activation thresholds for the excursion set (each element corresponding to one contrast). Remember that if a contrast is not specified, the average is found.}

\item{alpha}{(For inference only) Significance level for activation for the excursion set, or a vector thereof (each element corresponding to one contrast).}

\item{nsamp_theta}{Number of theta values to sample from posterior. Default is 50.}

\item{nsamp_beta}{Number of beta vectors to sample conditional on each theta value sampled. Default is 100.}

\item{verbose}{Should occasional updates be printed? Default: \code{TRUE}.}
}
\value{
A list with length equal to the number of subcortical models run in each of the single-subject data cases
}
\description{
This is a wrapper function that applies group modeling to subcortical
results. This currently only works for output from the EM implementation of
the Bayesian GLM.
}
