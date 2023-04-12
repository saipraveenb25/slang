# TODOs for Recompute insts.

1. Move the checkpoint policy compute and application sections to much later (after transposition). Carry the indexedRegionInfo from the unzip pass to allow primal hoisting later.

2. Create the policy itself earlier. Allow the unzip pass to add policy overrides for loop phis and the indices that it creates (i.e. must-store, must-invert)

3. Modify transposition step to simply ignore all primal insts. No need to invert insts. The exception is for loop counter parameters. Transposition should simply create a phi param for these (and add entry to inv-map). 

3.1. What do we do to try and remove loop counter generation in the unzip step?

4. Create and map a set of recompute blocks after the transposition step is done. This will create the primal->recompute map. We will also create a map from fwd-diff->primal so that we can lookup appropriate blocks for the loop counter insts that are in the diff blocks. This is the only exception that we need this for. Later, we can drop all primal inst handling in the reverse-mode blocks entirely. 

5. Run the policy & build list of policy classes (We can expand this later to have separate classes for in-region and cross-region) For recompute insts, copy them into the appropriate recompute block by looking up the primal->recompute map. Leave store insts where the are. For inverse insts, we run `invertInst()` and place the inverted insts in the block by looking up the recompute block for the target-inst (whose value is being generated through inversion) 
This will replace primal inst hoisting enirely.

6. Then, finally, call ensurePrimalAvailability(). This will lookup invMap with differential counter value (Soon, remove fwd-mode diff counters entirely, and replace wiht primal counter value) to find the appropriate inst.