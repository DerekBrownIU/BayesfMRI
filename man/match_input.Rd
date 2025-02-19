% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/util.R
\name{match_input}
\alias{match_input}
\title{Match user inputs to expected values}
\usage{
match_input(
  user,
  expected,
  fail_action = c("stop", "warning", "message", "nothing"),
  user_value_label = NULL
)
}
\arguments{
\item{user}{Character vector of user input. These will be matched to
\code{expected} using \code{match.arg()}.}

\item{expected}{Character vector of expected/allowed values.}

\item{fail_action}{If any value in \code{user} could not be
matched, or repeated matches occurred, what should happen? Possible values
are \code{"stop"} (default; raises an error), \code{"warning"}, and
\code{"nothing"}.}

\item{user_value_label}{How to refer to the user input in a stop or warning
message. If \code{NULL}, no label is used.}
}
\value{
The matched user inputs.
}
\description{
Match each user input to an expected/allowed value. Raise a warning if either
several user inputs match the same expected value, or at least one could not
be matched to any expected value. \code{ciftiTools} uses this function to
match keyword arguments for a function call. Another use is to match
brainstructure labels ("left", "right", or "subcortical").
}
\keyword{internal}
