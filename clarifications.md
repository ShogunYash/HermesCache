# Confirmed clarifications/Design Decisions
-  follow slides to max extent
-  if value was modified after it was requested by another cache , do e to m silent upgrade of the modified cache and the value in the other cache is the old value that was in memory and is temporarily incoherent
- on write miss if there is other caches in E or S even then get block always from memory and set others to invalid
	- In a BusRdX, if the cache line is in M state in another core, that supplier core first stalls for 100 cycles to write back the data to memory. After that, the requester core stalls for 100 cycles to fetch it. So the total delay is 200 cycles
	- but meanwhile the delay for each processor is 100 acc to sir "The total latency is 200 cycles, but each core stalls only for the part it is responsible for". but sensibly requesting core has to wait for the data to come ,till 200 cycles #ambiguous
  
- if the bus is busy then the processor cant handle snooping calls also, serialise and execute them in the order they were generated
- to transfer N blocks from cache to cache does supplier also halt for 2N cycles or only receiver.yes
- during a writeback the core is considered blocked.
- Simply holding the bus without completing a useful instruction is not considered an execution cycle, it will be idle if the core is stalled.Execution cycle happens only when the core is actively progressing its instruction.
- for invalidation u just need a free bus but it doesnt use the bus as such, and also doesnt take cycles
- 


# Ambiguous /Assuming
- write backs of M state cache on eviction happens seperately of cycle instructions , and it is an independent bus transaction!? to include cycles in execution of that core?
- what is execution cycles =total instructions + no of idle cycles(for cache to cache to transfer ,in case of requestor and sender should we count , those cycles also as busy )
- is copy back also taking 100 cycles or how is the design implementation , and is the core stalled in that time period and bus ?
- to transfer N blocks from cache to cache does supplier also halt for 2N cycles or only receiver.yes
- in case a core is putting any data on the bus/taking any data off the bus it is stalled? And if it is waiting to send/take data from the bus it is also stalled? But it is not stalled otherwise?
- 