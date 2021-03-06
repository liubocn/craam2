% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/RcppExports.R
\name{solve_mdp_rand}
\alias{solve_mdp_rand}
\title{Solves a plain Markov decision process with randomized policies.}
\usage{
solve_mdp_rand(mdp, discount, algorithm = "mpi", policy_fixed = NULL,
  maxresidual = 0.001, iterations = 10000L, timeout = 300,
  output_tran = FALSE, show_progress = TRUE)
}
\arguments{
\item{mdp}{A dataframe representation of the MDP. Each row
represents a single transition from one state to another
after taking an action a. The columns are:
idstatefrom, idaction, idstateto, probability, reward}

\item{discount}{Discount factor in [0,1]}

\item{algorithm}{One of "mpi", "vi", "vi_j", "pi"}

\item{policy_fixed}{States for which the  policy should be fixed. This
should be a dataframe with columns idstate, idaction, probability.
 The policy is optimized only for states that are missing, and the
 fixed policy is used otherwise}

\item{maxresidual}{Residual at which to terminate}

\item{iterations}{Maximum number of iterations}

\item{timeout}{Maximum number of secods for which to run the computation}

\item{output_tran}{Whether to construct and return a matrix of transition
probabilites and a vector of rewards}

\item{show_progress}{Whether to show a progress bar during the computation}
}
\value{
A list with value function policy and other values
}
\description{
The method can be provided with a randomized policy for some states
and the output policy is randomized.
}
