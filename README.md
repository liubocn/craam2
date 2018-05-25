# CRAAM: Robust And Approximate Markov decision processes #


[![Build Status](https://travis-ci.org/marekpetrik/CRAAM.svg?branch=master)](https://travis-ci.org/marekpetrik/CRAAM)

Craam is a **header-only** C++ library for solving Markov decision processes with support for handling uncertainty in transition probabilities. The library can handle uncertainties using both *robust*, or *optimistic* objectives.

The library includes Python and R interfaces. See below for detailed installation instructions.

When using the *robust objective*, adversarial nature chooses the worst plausible realization of the uncertain values. When using the *optimistic objective*, collaborative nature chooses the best plausible realization of the uncertain values. 

The library also provides tools for *basic simulation*, for constructing MDPs from *sample*s, and *value function approximation*. Objective functions supported are infinite horizon discounted MDPs, finite horizon MDPs, and stochastic shortest path \[Puterman2005\]. Some basic stochastic shortest path methods are also supported. The library assumes *maximization* over actions. The number of states and actions must be finite.

The library is based on two main data structures: MDP and RMDP. **MDP** is the standard model that consists of states 𝒮 and actions 𝒜. Note that robust solutions are constrained to be **absolutely continuous** with respect to *P*(*s*, *a*, ⋅). This is a hard requirement for all choices of ambiguity (or uncertainty).

The **RMPD** model adds a set of *outcomes* that model possible actions that can be taken by nature. Using outcomes makes it more convenient to capture correlations between the ambiguity in rewards and the uncertainty in transition probabilities. It also make it much easier to represent uncertainties that lie in small-dimensional vector spaces. Constraints for nature's distributions over outcomes are also supported.

The available algorithms are *value iteration* and *modified policy iteration*. The library support both the plain worst-case outcome method and a worst case with respect to a base distribution.

A python interface is also supported. See the installation instructions below.

## Build and Run Command-line Executable ##

To run a benchmark problem, download and decompress one of the following test files:

-   Small problem with 100 states: <https://www.dropbox.com/s/b9x8sz7q5ow1vm4/ss.zip>
-   Medium problem with 2000 states (7zip): <https://www.dropbox.com/s/k0znc23xf9mpe5i/ms.7z>

These two benchmark problems were generated from a uniform random distribution.

First, download the code.

``` bash
    $ git clone --depth 1 https://github.com/marekpetrik/CRAAM.git
    $ cmake -DCMAKE_BUILD_TYPE=Release .
    $ cmake --build . --target craam-cli
```

Second, install Eigen in the same directory.

``` bash
    $ cd craam
    $ wget http://bitbucket.org/eigen/eigen/get/3.3.4.tar.gz
    $ tar xzf 3.3.4.tar.gz
    $ rm 3.3.4.tar.gz
    $ mv eigen-eigen-5a0156e40feb/Eigen ../include
```


Finally, download and solve a simple benchmark problem:

``` bash
    $ mkdir data
    $ cd data
    $ wget https://www.dropbox.com/s/b9x8sz7q5ow1vm4/ss.zip
    $ unzip ss.zip
    $ cd ..
    $ bin/craam-cli -i data/smallsize_test.csv -o data/smallsize_policy.csv
```

To see the list of command-line options, run:

``` bash
    $ bin/craam-cli -h
```


## Installing C++ Library ##

It is sufficient to copy the entire root directory to a convenient location.

Numerous asserts are enabled in the code by default. To disable them, insert the following line *before* including any files:

``` cpp
#define NDEBUG
```

To make sure that asserts are disabled, you may also want to double check the file `/craam/config.hpp` which is auto-generated by `cmake`.

The library has minimal dependencies and was tested on Linux. It has not been tested on Windows or MacOS.

### Requirements

-   C++14 compatible compiler:
    -   Tested with Linux GCC 4.9.2,5.2.0,6.1.0; does not work with GCC 4.7, 4.8.
    -   Tested with Linux Clang 3.6.2 (and maybe 3.2+).
-   [Eigen](http://eigen.tuxfamily.org) 3+ for computing occupancy frequencies

#### Optional Dependencies

-   [CMake](http://cmake.org/): 3.1.0 to build tests, command line executable, and the documentation
-   [OpenMP](http://openmp.org) to enable parallel computation
-   [Doxygen](http://doxygen.org%3E) 1.8.0+ to generate documentation
-   [Boost](http://boost.org) for compiling and running unit tests (`boost-devel` package)

### Documentation

The project uses [Doxygen](http://www.stack.nl/~dimitri/doxygen/) for the documentation. To generate the documentation after generating the files, run:

``` bash
    $ cmake --build . --target docs
```

This automatically generates both HTML and PDF documentation in the folder `out`.

### Run unit tests

Note that Boost must be present in order to build the tests in the first place.

``` bash
    $ cmake .
    $ cmake --build . --target testit
```


## Installing Python Interface ##

A python interface is provided by package `craam`. Most of the classes and methods are contained in `craam.crobust`.

### Requirements ###

-   Python 3.5+ (Python 2 is NOT supported)
-   Setuptools 7.0+
-   Numpy 1.8+
-   Cython 0.24+

### Installation ###


The python interface that comes with this project is just a thin interface to the C++ code. [RAAM](https://github.com/marekpetrik/raam) is a more full-featured library that add significant python functionality as well as unit tests.

To install the Python extension, first compile the C++ library as described above. Then go to the `python` subdirectory and run:

``` bash
  $ python3 setup.py install --user 
```

Omit `--user` to install the package for all users rather than just the current one.

## Installing R Interface ##

The R interface is experimental and has very limited functionality. Method signatures are expected to change. The package should work on Linux, Mac, and Windows (with RTools 3.4+).

Te package can be installed directly from the github repository using devtools:
```R
library(devtools)
devtools::install_github("marekpetrik/craam/rcraam")
```

Alternatively, install from bitbucket (less stable):

```R
library(devtools)
devtools::install_bitbucket("marekpetrik/craam/rcraam")
```

R version 3.4 is recommended, but the package probably works with earlier versions too.

The following short program can be used to load and solve an MDP:

```R
library(rcraam)
m <- MDP()
data <- read.csv("mdp.csv")
m$from_dataframe(data)
m$solve_mpi(0.95)
```


## C++ Development ##


The instruction above generate a release version of the project. The release version is optimized for speed, but lacks debugging symbols and many intermediate checks are eliminated. For development purposes, is better to use the Debug version of the code. This can be generated as follows:

``` bash
    $ cmake -DCMAKE_BUILD_TYPE=Debug .
    $ cmake --build .
```

To help with development, Cmake can be used to generate a [CodeBlocks](http://www.codeblocks.org/) project files too:

``` bash
  $ cmake . -G "CodeBlocks - Ninja"
```

To list other types of projects that Cmake can generate, call:

``` bash
  $ cmake . -G
```

### Installing Python Package ###

A convenient way to develop Python packages is to install them in the development mode as:

``` bash
  $ python3 setup.py develop --user 
```

In the development mode, the python files are not copied on installation, but rather their development version is used. This means that it is not necessary to reinstall the package to reflect code changes. **Cython note**: Any changes to the cython code require that the package is rebuilt and reinstalled.

## Next Steps ##


### C++ Library ###


See the [online documentation](http://cs.unh.edu/~mpetrik/code/craam) or generate it locally as described above.

Unit tests provide some examples of how to use the library. For simple end-to-end examples, see `tests/benchmark.cpp` and `test/dev.cpp`. Targets `BENCH` and `DEV` build them respectively.

The main models supported are:

-   `craam::MDP` : plain MDP with no specific definition of ambiguity (can be used to compute robust solutions anyway)
-   `craam::RMDP` : an augmented model that adds nature's actions (so-called outcomes) to the model for convenience
-   `craam::impl::MDPIR` : an MDP with implementability constraints. See \[Petrik2016\].

The regular value-function based methods are in the header `algorithms/values.hpp` and the robust versions are in in the header `algorithms/robust_values.hpp`. There are 4 main value-function based methods:

-   `solve_vi`: Gauss-Seidel value iteration; runs in a single thread. -`solve_mpi`: Jacobi modified policy iteration; parallelized with OpenMP. Generally, modified policy iteration is vastly more efficient than value iteration.
-   `rsolve_vi`: Like the value iteration above, but also supports robust, risk-averse, or optimistic objectives.
-   `rsolve_mpi`: Like the modified policy iteration above, but it also supports robust, risk-averse, optimistic objective.

These methods can be applied to eithen an MDP or an RMDP.

The header `algorithms/occupancies.hpp` provides tools for converting the MDP to a transition matrix and computing the occupancy frequencies.

There are tools for building simulators and sampling from simulations in the header `Simulation.hpp` and methods for handling samples in `Samples.hpp`.

### Python Interface ###

The python interface closely mirrors the C++ classes. The following main types of plain and robust MDPs supported:

-   `craam.MDP` : plain MDP with no specific definition of ambiguity (can be used to compute robust solutions anyway)
-   `craam.RMDP` : an augmented model that adds nature's actions (so-called outcomes) to the model for convenience
-   `craam.MDPIR` : an MDP with implementatbility constraints. See \[Petrik2016\].

The classes support the following main optimization algorithms:

`solve_vi`: Gauss-Seidel value iteration; runs in a single thread. `solve_mpi`: Jacobi modified policy iteration; parallelized with OpenMP. Generally, modified policy iteration is vastly more efficient than value iteration. `rsolve_vi`: Like the value iteration above, but also supports robust, risk-averse, or optimistic objectives. `rsolve_mpi`: Like the modified policy iteration above, but it also supports robust, risk-averse, optimistic objective.

States, actions, and outcomes (actions of nature) are represented by 0-based contiguous indexes. The actions are indexed independently for each state and the outcomes are indexed independently for each state and action pair.

Transitions are added through function `add_transition`. New states, actions, or outcomes are automatically added based on the new transition.

Other classes are available to support simulating MDPs and constructing them from samples:

-   `craam.crobust.SimulatorMDP` : Simulates an MDP for a given deterministic or randomized policy
-   `craam.crobust.DiscreteSamples` : Collection of state to state transitions as well as samples of initial states. All states and actions are identified by integers.
-   `craam.crobust.SampledMDP` : Constructs an MDP from samples in `DiscreteSamples`.

### Solving a Simple MDP ###

The following code solves a simple MDP problem precisely using modified policy iteration.

``` python
from craam import crobust
import numpy as np

states = 100
P1 = np.random.rand(states,states)
P1 = np.diag(1/np.sum(P1,1)).dot(P1)
P2 = np.random.rand(states,states)
P2 = np.diag(1/np.sum(P2,1)).dot(P2)
r1 = np.random.rand(states)
r2 = np.random.rand(states)

transitions = np.dstack((P1,P2))
rewards = np.column_stack((r1,r2))

mdp = crobust.MDP(states,0.99)
mdp.from_matrices(transitions,rewards)
value,policy,residual,iterations = mdp.solve_mpi(100)

print('Value function s0-s9:', value[:10])
```

    ## Value function s0-s9: [ 69.80128537  70.42106693  70.38961762  70.22134631  70.40475099
    ##   69.86828576  69.94327196  70.36343291  70.21632776  69.95414206]

This example can be easily converted to a robust MDP by appropriately defining additional outcomes (the options available to nature) with transition matrices and rewards.

## References ##


-   \[Filar1997\] Filar, J., & Vrieze, K. (1997). Competitive Markov decision processes. Springer.
-   \[Puterman2005\] Puterman, M. L. (2005). Markov decision processes: Discrete stochastic dynamic programming. Handbooks in operations research and management …. John Wiley & Sons, Inc.
-   \[Iyengar2005\] Iyengar, G. N. G. (2005). Robust dynamic programming. Mathematics of Operations Research, 30(2), 1–29.
-   \[Petrik2014\] Petrik, M., Subramanian S. (2014). RAAM : The benefits of robustness in approximating aggregated MDPs in reinforcement learning. In Neural Information Processing Systems (NIPS).
-   \[Petrik2016\] Petrik, M., & Luss, R. (2016). Interpretable Policies for Dynamic Product Recommendations. In Uncertainty in Artificial Intelligence (UAI).
