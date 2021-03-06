# requires that python reticulate is installed
# need to run pip install 
suppressMessages(library(ggplot2))
suppressMessages(library(dplyr))
library(readr)       # write CSV quickly
library(reticulate)  # run open ai gym
library(splines)     # approximate dynamics
library(nabor)       # nearest neighbor (fast) that finds ids of neighbors
loadNamespace("reshape2") # process nabor output
loadNamespace("tidyr")    # plot value functions
library(assertthat)

#source("discretized.R")

# ------ Program parameters ------

# parameters
plot.fits <- FALSE
standardize.features <- TRUE
discount <- 0.9999

# synthetic model
state.count <- 3000           # synthetic states
syn.sample.count <- 10000      # synthetic samples
use.low.discrepancy <- FALSE   # whether to use low discrepancy functions to generate synthetic samples

# prediction model
spline.count <- 3

# mirror samples (whether double samples by creating a mirror image)
sample.episodes <- 100
mirror.samples <- TRUE

# features
feature.names <- c("CartPos","CartVelocity","PoleAngle", "PoleVelocity")
action.names <- c(0,1)
limits.lower <- c(-2.4, -3, -0.25, -3.1)
limits.upper <- c(2.4, 3, 0.25, 3.1)

#* add states that are nearby to state being transitioned into: 
#* this is to allow robust transitions to 
#* states which may not be seen in the sample, but are possible
#* the transition probabilities to those states will be 0
fuzzy.neighbors <- 20
robustness <- 0.05
fuzzy.radius <- 0.2

# ------ Generate samples ------

code_generate_samples <- paste0("
import random
import gym
env = gym.make('CartPole-v1')
random.seed(2018)

laststate = None
samples = []
for k in range(",sample.episodes,"):
  env.reset()
  done = False
  for i in range(100):
    #env.render()
    action = env.action_space.sample()
    if i > 0:
      samples.append( (i-1,) + tuple(state) + (action,) + (reward,) )
    # stop only after saving the state
    if done:
      break
    [state,reward,done,info] = env.step(action) # take a random action
env.close()
")

cat("Simulating random policy cart-pole ...\n")
res <- py_run_string(code_generate_samples)
samples <- as.data.frame(t(sapply(res$samples, unlist)))
colnames(samples) <- c("Step", feature.names,"Action","Reward")

# alternatively: load samples
#samples <- read_csv("cartpole.csv")

# ------ Prepare true samples -----

# connect samples
cat("Generated", nrow(samples), "sample transitions.\n")
samples_step <- samples
for(f in feature.names){
  samples_step[[paste0(f, "N")]] <- lead(samples_step[[f]], 1)  
}
samples_step <- samples_step %>% 
  mutate(StepN=lead(Step,1), Final=!(lead(Step,2) == Step + 1)) %>%
  filter(StepN == Step + 1) %>%
  na.omit()

# double samples by mirroring the left and right sides
if(mirror.samples){
  samples_mirror <- samples_step 
  for(f in feature.names){
    samples_mirror[[f]] <- -samples_mirror[[f]]
    samples_mirror[[paste0(f, "N")]] <- -samples_mirror[[paste0(f, "N")]]
  }
  samples_mirror$Action <- 1 - samples_mirror$Action
  samples_step <- rbind(samples_step, samples_mirror)  
  rm(samples_mirror)
}

# ------ Fit a linear model -------
fit_data <- function(samples_step){
  actions <- unique(samples_step$Action)
  models <- list()
  for(a in actions){
    as <- paste0("A",a)
    models[[as]] <- list()
    
    for(f in feature.names){
      subset.data <- samples_step %>% filter(Action == !!a)
      # make prediction as a linear combination of all current feature names
      formula.fit <- paste(sapply(feature.names, 
                                  function(x){paste0("ns(", x, ", spline.count)")}), 
                           collapse = "+")
      # append N to the end of the predicted feature name
      formula.fit <- paste0(f,"N~", formula.fit)
      models[[as]][[f]] <- lm(formula(formula.fit), data=subset.data)
      cat("Prediction: action", as, "feature:", f, " = r.squared", 
          summary(models[[as]][[f]])$r.squared,"\n")
    }
  }
  return(models)
}

models <- fit_data(samples_step)

# ------ Predict next state (methods) -------

#' Predicts the next state transition for many states simultaneously
#' @param states.current A data frame with columns:
#'  CartPos, CartVelocity,PoleAngle,PoleVelocity
#' @param action A single action that is taken for all states
predict.next.many <- function(models, states.current, action){
  action_string <- paste0("A",action)
  result <- list()
  for(f in feature.names){
      result[[f]] <- predict(models[[action_string]][[f]], states.current)
  }
  return(as.data.frame(result))
}

predict.reward <- function(stateaction){
  # state[1] = $cart.pos
  # state[3] = $pole.angle
  ifelse(abs(stateaction[1]) < 2.0 && 
         abs(stateaction[3]) < (12 / 180 * pi), 1, 0)
}
# ------ Construct synthetic samples -------

# Build samples of possible states. These samples will be later aggregated with random 
# locations to determine the actual states.

cat("Generating", syn.sample.count, "synthetic samples from the model .... \n")

# generate the fake samples
if(use.low.discrepancy){
  loadNamespace("randtoolbox") # only needed for low-discrepancy sampling (use.low.discrepancy == TRUE)
  # using a low-discrepancy metric to generate samples that
  # better cover the space (library randtoolbox)
  samples.gen <- randtoolbox::torus(syn.sample.count, dim = length(feature.names)) %*%
    diag(limits.upper - limits.lower) + 
    rep(1,syn.sample.count) %*% matrix(limits.lower, 1)
  
}else{
  samples.gen <- 
    do.call(cbind, 
            lapply(1:length(feature.names), function(i){
              runif(syn.sample.count,  
                    min=limits.lower[i], 
                    max=limits.upper[i])}))
}

# Construct predicted transition samples 
samples.actions <- do.call(
  rbind, lapply(action.names, 
    function(x){
      cbind(samples.gen, matrix(x,syn.sample.count,1))
    }))

# construct a data frame with all states that initiate transitions
samples.first <- as.data.frame(samples.gen)
colnames(samples.first) <- feature.names
                                
samples.nexts <- as.matrix(
  do.call(rbind,
          lapply(action.names,
                  function(act){
                    predict.next.many(models,samples.first,act)               
                  }) ))

samples.rewards <- apply(samples.actions, 1, predict.reward)

# Select the actual states randomly from the samples. 
# Since the samples are randomly generated in the first place, we can simply take
# the first $n$ to get a random selection. This also makes a lot of sense
# when using low-discrepancy generated states (the first n will also have low dicrepancy)
states <- samples.gen[1:state.count,]

# Compute scales for each direction to be used with the nearest sample identification. 
# WARNING: it is important to use the same scales when the policy is implemented.
if(standardize.features){
  # Important: assumes that samples are zero-centered
  scales <- limits.upper -limits.lower #apply(samples, 2, function(x){max(x) - min(x)}) # compute the span of the feature
  scales <- diag(1/scales) # diag(1/scales[c('CartPos', 'CartVelocity','PoleAngle','PoleVelocity')])  
}else{
  scales <- diag(c(1,1,1,1))  
}

write_csv(as.data.frame(scales), 'scales.csv')

# Construct a data frame with the samples. Warning! KNN1 returns a factor, needs to be turned 
# to an integer. Also, it is necessary to scale the features, the scale of 
# the velocities is very different from the scale of the position and the angle.

# --------Build MDP from samples ---------------------

cat("Building MDP .... \n")
# repeat once for each action
state.ids.from <- as.integer(rep(class::knn1(states %*% scales, 
                                             samples.gen %*% scales, 
                                             1:state.count ), 2)) - 1
# warning: (0:(state.count - 1) ) does not work

# the initial batch of samples should be just the
# consequtive state ids (since the state center is closest to itself)
assert_that(all(state.ids.from[1:state.count] == 0:(state.count - 1)))

state.ids.to <- as.integer(class::knn1(states %*% scales, 
                                       samples.nexts %*% scales, 
                                       1:state.count )) - 1
# warning: (0:(state.count - 1) ) does not work

samples.frame <- data.frame(idstatefrom = state.ids.from,
                            idaction = samples.actions[,5],
                            idstateto = state.ids.to,
                            reward = samples.rewards)


# --------Construct fuzzy neighbors ---------------------

# this assumes that there is some unsampled probablity that the
# transition is to a state that is near a sampled state
if(fuzzy.neighbors > 1){
  fuzzy.states.to <- nabor::knn(as.data.frame(states %*% scales), 
                                as.data.frame(samples.nexts %*% scales), 
                                fuzzy.neighbors,
                                radius = fuzzy.radius)$nn.idx[,2:fuzzy.neighbors] - 1
  
  # idtransition represents the index of the transition in samples.frame
  # if the radius is too small then there will be some idstateto < 0
  fuzzy.transitions <- 
    reshape2::melt(fuzzy.states.to, varnames = c("idtransition", "sample"), 
                   value.name="idstateto") %>%
    filter(idstateto >= 0)
    
  # right join to address the possibility that there are no
  # fuzzy transitions for some states
  fuzzy.transitions <- 
    right_join(samples.frame %>% mutate(idtransition = row_number()) %>%
                                select(-idstateto),
              fuzzy.transitions,
              by = "idtransition") %>% na.fail() %>%
    select(-sample, -idtransition) %>%
    mutate(probability = 0)
}

# -------Solve MDP and save policy -------

mdp <- rcraam::mdp_from_samples(samples.frame)

# stop if there are any na values in the MDP
if(fuzzy.neighbors >= 1){
  mdp <- bind_rows(mdp, fuzzy.transitions) %>% na.fail()
}

write_csv(mdp, "cartpole_mdp.csv")
cat ("Solving MDP ... \n")
solution <- rcraam::solve_mdp(mdp, discount, algorithm="mpi", iterations = 1000, 
                              maxresidual = 0.01)

# solution with a low discount factor
cat ("Solving MDP (low discount) ... \n")
solution_ldisc <- rcraam::solve_mdp(mdp, 0.9, algorithm="mpi", iterations = 1000, 
                                    maxresidual = 0.01)

#cat ("Solving RMDP VI ... \n")
#rsolution_vi <- rcraam::rsolve_mdp_sa(mdp, discount, "l1u", 0.1, algorithm="vi",
#                                                                 iterations = 10000)
cat ("Solving RMDP MPPI ... \n")
rsolution_mppi <- rcraam::rsolve_mdp_sa(mdp, discount, "l1u", robustness, algorithm="mppi",
                                                                  iterations = 5000,
                                                                  maxresidual = 0.01)

  # Compare value functions
vfs <- inner_join(solution$valuefunction %>% rename(vf = value), 
                  rsolution_mppi$valuefunction %>% rename(rvf = value),
                  by = 'idstate') %>% 
  arrange(vf / max(vf) + rvf/max(rvf)) %>% mutate(x = row_number()) %>% 
  mutate(vf = vf /max(vf), rvf = rvf/max(rvf)) %>%
  tidyr::gather("algorithm", "value", vf, rvf)
print(ggplot(vfs, aes(x=x, y=value, color=algorithm)) + geom_line() )

#' Saves the solution so it can be consumed by a python script
save.solution <- function(mdp, solution){
  qvalues <- rcraam::compute_qvalues(mdp, discount, solution$valuefunction)
  
  # Save solution to a data_frame
  states.scaled <- states %*% scales
  pol.states <- solution$policy$idstate
  pol.actions <- solution$policy$idaction
  solution.df <- data.frame(CartPos = states.scaled[pol.states+1,1],
                            CartVelocity = states.scaled[pol.states+1,2],
                            PoleAngle = states.scaled[pol.states+1,3],
                            PoleVelocity = states.scaled[pol.states+1,4],
                            State = pol.states,
                            Action = pol.actions,
                            Value = solution$valuefunction$value[pol.states+1])
  # add probabilities to the file if the policy is randomized
  if("probability" %in% colnames(solution$policy)){
    solution.df$Probability <- solution$policy$probability
  }
  
  # ------------- Save values -----------------------
  write_csv(samples, "samples.csv")
  write_csv(solution.df, "policy_nn.csv")
  write_csv(qvalues, "qvalues.csv")
}

save.solution(mdp, solution_ldisc)
save.solution(mdp, solution)
save.solution(mdp, rsolution_mppi)

# ------ Plot Fits ---------

if(plot.fits){
  ggplot(samples_step, 
         aes(x=CartPos, y=CartPosN, color=as.factor(Action))) + 
    geom_point() + theme_light()
  
  ggplot(samples_step %>% filter(Action==0), 
         aes(x=PoleAngle, y=CartVelocityN-CartVelocity, 
             color=PoleVelocity)) +
    geom_point() + theme_light()
  
  ggplot(samples_step %>% filter(Action==1), 
         aes(x=CartVelocity, y=CartVelocityN, 
             color=PoleVelocity)) +
    geom_point() + theme_light()
  
  ggplot(samples_step, 
         aes(x=CartVelocity, y=(PoleVelocity+CartVelocity),color=PoleAngle)) + 
    geom_point() + theme_light()
  
  ggplot(samples_step %>% filter(Action==0), 
         aes(x=PoleVelocity,y=PoleAngleN-PoleAngle,color=CartVelocity )) + 
    geom_point() + theme_light()
  
  ggplot(samples_step %>% filter(Action==1), 
         aes(x=PoleAngle,y=PoleVelocityN - PoleVelocity, color=CartVelocity )) + 
    geom_point() + theme_light()
}

