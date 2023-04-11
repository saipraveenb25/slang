# TODOs for Recompute insts.

1. `createRecomputeRegion()` for cloning the primal control flow over to the differential blocks. Mark the blocks IRPrimalRecomputeDecoration to be clear about what they are. Always include a blank last block where the transposition pass can place additional insts without worrying about accidentally moving them before their use-defs.

2. Add recompute insts to the list of insts processed by `ensurePrimalAvailability()`, and add a check to ignore indexed primal-recompute insts in the differential loops (these do not need indexed read/writes), but standard control flow still needs to be handled.

3. Adjust `reverseCFGRegions()` to reverse the differential blocks, ignore the primal recompute blocks, and wire the last block of the primal recompute region to the first block of the differential region.

4. Change the primal inst hoisting mechanism in the transposition step to simply move everything to the first recompute block that we can find while moving backwards through the blocks.

5. Create `emplaceRecomputeRegions()` to move recompute blocks in each recompute region to the corresponding reverse-mode region. There is a corner case for loop condition blocks. For these, the recompute block must be merged with the existing loop condition block (with the former's insts going first). But where do we put the recompute insts for the loop condition block? We could place the insts along with differential insts in the fwd-mode loop condition block, then when emplacing recompute blocks, we check to see if the source is both differential & recompute, in which case, we look up the appropriate reverse-mode block and merge the primal insts in there.


# Different approach.

1. Move the checkpoint policy compute and application sections to much later (after transposition). Carry the indexedRegionInfo from the unzip pass to allow primal hoisting later.

2. Create the policy itself earlier. Allow the unzip pass to add policy overrides for loop phis and the indices that it creates (i.e. must-store, must-invert)

3. Modify transposition step to simply ignore all primal insts. No need to invert insts. The exception is for loop counter parameters. Transposition should simply create a phi param for these (and add entry to inv-map)

4. Create and map a set of recompute blocks after the transposition step is done. This will create the primal->recompute map. We will also create a map from fwd-diff->primal so that we can lookup appropriate blocks for the loop counter insts that are in the diff blocks. This is the only exception that we need this for. Later, we can drop all primal inst handling in the reverse-mode blocks entirely. 

5. Run the policy & build list of policy classes (We can expand this later to have separate classes for in-region and cross-region) For recompute insts, copy them into the appropriate recompute block by looking up the primal->recompute map. Leave store insts where the are. For inverse insts, we run `invertInst()` and place the inverted insts in the block by looking up the recompute block for the target-inst (whose value is being generated through inversion)
This will replace primal inst hoisting enirely.

6. Then, finally, call ensurePrimalAvailability(). This will lookup invMap with differential counter value (Soon, remove fwd-mode diff counters entirely, and replace wiht primal counter value) to find the appropriate inst.