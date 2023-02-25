# Notes
- 1 buy and 1 sell
- buy and sell atomic booleans
- semaphore turnstile for ensuring that the buy waits for the sell to insert (and vice-versa)

- assume we are buy.
- test and set the buy flag :w

- loop while count > 0.
- acquire turnstile and release (basically wait for sell to insert)
- acquire lock for sell binary search tree (to ensure that sell does not insert at the same time.)
- take the best price and the first order at that price.
- do matching
- insert remaining sell back if there exists one
- release lock for sell BST

-after the for loop,
- acquire turnstile
- acquire lock for buy BST and insert remaining buy order if there is remaining.
