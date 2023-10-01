# Superscalar-Pipeline-with-Branch-Prediction
<img src="https://github.com/adithi-su/Superscalar-Pipeline-with-Branch-Prediction/blob/master/pipeline_img.png" alt="5 stage pipeline" title="Five stage pipeline"> 
Figure 1: Five stage pipeline

## Deliverables:
### Part A: Dependency check
* Implement data dependency tracking and related stalls for a scalar machine (N=1)
* Generalize for N-wide super-scalar
* Implement Data Forwarding (from both MEM and EX)

### Part B: Branch Prediction
* Implement an “AlwaysTaken” predictor, and integrate it with the pipeline
* Implement a Gshare predictor with HistoryLength=12
<img src="https://github.com/adithi-su/Superscalar-Pipeline-with-Branch-Prediction/blob/master/gshare.png" alt="Gshare" height=50% width=50%>
Figure 2: GShare branch prediction
