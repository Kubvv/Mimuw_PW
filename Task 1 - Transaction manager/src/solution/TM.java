package cp1.solution;

import cp1.base.*;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Stack;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Semaphore;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public class TM implements cp1.base.TransactionManager
{
    /* ConcurrentHashMaps */
    //Information whether an active transaction is aborted or not
    private final ConcurrentHashMap<Thread, Boolean> threadsPresent;
    //Time of transaction start for every thread;
    private final ConcurrentHashMap<Thread, Long> threadsTime;
    //Stack of performed operations for every active thread
    private final ConcurrentHashMap<Thread, Stack<ResourcePair>> operationHistory;
    //Who owns resource with given index
    private final ConcurrentHashMap<Integer, Thread> ownership;
    //What is the index of resource that a thread is waiting for
    private final ConcurrentHashMap<Thread, Integer> lockedOn;

    /* Semaphores and atomic variables */
    private final Semaphore firMutex = new Semaphore(1, true);
    private final Semaphore secMutex = new Semaphore(1, true);
    private final AtomicBoolean allowLockCheck = new AtomicBoolean(false);
    private final AtomicBoolean isLockActive = new AtomicBoolean(false);
    private final AtomicInteger currentCommitters = new AtomicInteger(0);
    private final AtomicInteger stoppedCommitters = new AtomicInteger(0);
    private final Semaphore commitWait = new Semaphore(0, true);
    private final Semaphore lockWait = new Semaphore(0, true);

    private final Resource[] resources; //Array of resources
    private final LocalTimeProvider LTP; //Local time provider

    /* TM Constructor */
    TM(Collection<Resource> resources, LocalTimeProvider LTP)
    {
        this.resources = resources.toArray(new Resource[resources.size()]);
        this.LTP = LTP;
        threadsPresent = new ConcurrentHashMap<Thread, Boolean>();
        threadsTime = new ConcurrentHashMap<Thread, Long>();
        operationHistory = new ConcurrentHashMap<Thread, Stack<ResourcePair>>();
        ownership = new ConcurrentHashMap<Integer, Thread>();
        lockedOn = new ConcurrentHashMap<Thread, Integer>();
    }

    @java.lang.Override
    public void startTransaction() throws AnotherTransactionActiveException
    {
        //Throw if thread has active transaction
        if (threadsPresent.containsKey(Thread.currentThread()))
        {
            throw new AnotherTransactionActiveException();
        }
        threadsTime.put(Thread.currentThread(), LTP.getTime());
        threadsPresent.put(Thread.currentThread(), false);
        operationHistory.put(Thread.currentThread(), new Stack<ResourcePair>());
    }

    @java.lang.Override
    public void operateOnResourceInCurrentTransaction(ResourceId rid, ResourceOperation operation)
            throws NoActiveTransactionException, UnknownResourceIdException, ActiveTransactionAborted, ResourceOperationException, InterruptedException
    {
        /* Exception handling */
        if (!isTransactionActive()) {
            throw new NoActiveTransactionException();
        }
        int currResource = findResource(rid);
        if (currResource == -1) {
            throw new UnknownResourceIdException(rid);
        }
        if (isTransactionAborted()) {
            throw new ActiveTransactionAborted();
        }

        if (ownership.get(currResource) != Thread.currentThread())
        {
            //check if deadlock was created
            firMutex.acquire();
            Thread youngestLocker = findLock(currResource);
            firMutex.release();
            if (youngestLocker != null)
            {
                //deadlock was found, abort youngest thread and interrupt it
                threadsPresent.put(youngestLocker, true);
                youngestLocker.interrupt();
            }

            //Wait until resource becomes available
            synchronized (resources[currResource]) {
                while (ownership.containsKey(currResource))
                {
                    try {
                        resources[currResource].wait();
                    }
                    catch (InterruptedException e) {
                        if (isTransactionAborted())
                        {
                            throw new ActiveTransactionAborted();
                        }
                        else
                        {
                            throw e;
                        }
                    }
                }
            }

            ownership.put(currResource, Thread.currentThread());
            lockedOn.remove(Thread.currentThread());
        }

        resources[currResource].apply(operation);
        Stack<ResourcePair> temp = operationHistory.get(Thread.currentThread());
        temp.push(new ResourcePair(currResource, operation));
        operationHistory.put(Thread.currentThread(), temp);
    }

    @java.lang.Override
    public void commitCurrentTransaction() throws NoActiveTransactionException, ActiveTransactionAborted
    {
        /* Exception handling */
        Stack<ResourcePair> toRollback = operationHistory.get(Thread.currentThread());
        if (toRollback == null)
        {
            throw new NoActiveTransactionException();
        }
        if (isTransactionAborted())
        {
            throw new ActiveTransactionAborted();
        }

        waitForLock();
        //Remove ownership of all your resources
        while (!toRollback.empty())
        {
            ResourcePair pair = toRollback.pop();
            removeOwnership(pair.getIndex());
        }
        clearThread();
        releaseLocks();
    }

    @java.lang.Override
    public void rollbackCurrentTransaction()
    {
        waitForLock();
        Stack<ResourcePair> toRollback = operationHistory.get(Thread.currentThread());
        if (toRollback != null) {
            //Undo all operations in your history
            while (!toRollback.empty()) {
                ResourcePair pair = toRollback.pop();
                resources[pair.getIndex()].unapply(pair.getResourceOper());
                removeOwnership(pair.getIndex());
            }
        }
        clearThread();
        releaseLocks();
    }

    @java.lang.Override
    public boolean isTransactionActive()
    {
        return (threadsPresent.containsKey(Thread.currentThread()));
    }

    @java.lang.Override
    public boolean isTransactionAborted()
    {
        return (threadsPresent.containsKey(Thread.currentThread()) && threadsPresent.get(Thread.currentThread()));
    }

    //Return the index of a resource with given id, return -1 if not found
    private int findResource(ResourceId rid)
    {
        int result = -1;

        for (int i = 0; i < resources.length && i != -1; i++)
        {
            if (resources[i].getId() == rid)
            {
                result = i;
            }
        }

        return result;
    }

    /* Check if there is a deadlock, where deadlock is a cycle in a directed graph G = (V,E),
    where vertices are represented by threads and set of their resources, and edges are represented
    as the fact that thread waits for a resource that another thread owns. Return null if there is no deadlock,
    and if deadlock was found, return thread that is the youngest in the cycle.
     */
    private Thread findLock(Integer currResource)
    {
        waitForCommits();
        boolean done = false;
        Thread result = null;
        Thread self = Thread.currentThread();
        Thread current = Thread.currentThread();
        Integer currWants = currResource;
        ArrayList<Thread> visited = new ArrayList<>();

        lockedOn.putIfAbsent(Thread.currentThread(), currResource);

        while (!done)
        {
            visited.add(current); //Add thread to array of visited threads
            currWants = lockedOn.get(current);
            if (currWants != null)
            {
                current = ownership.get(currWants); //Go to the next vertex
                if (current == self)
                {
                    //We found the cycle
                    done = true;
                    result = findYoungest(visited);
                    lockedOn.remove(result);
                }
                else if (current == null)
                {
                    done = true;
                }
            }
            else
            {
                //current thread doesn't wait for anything, thus there is no cycle
                done = true;
            }
        }
        releaseCommits();
        return result;
    }

    //Find the youngest thread in a given array of threads.
    private Thread findYoungest(ArrayList<Thread> t)
    {
        Thread result = null;
        for (Thread thread : t) {
            result = determineYoungest(result, thread);
        }
        return result;
    }

    /* Compare two threads and return the one which is youngest.
     If both have the same time of start, return the one which has bigger id.*/
    private Thread determineYoungest(Thread currYoungest, Thread contender)
    {
        Thread result = currYoungest;
        if (currYoungest == null || threadsTime.get(currYoungest) < threadsTime.get(contender))
        {
            result = contender;
        }
        else if (threadsTime.get(currYoungest).equals(threadsTime.get(contender)) &&
                currYoungest.getId() < contender.getId())
        {
            result = contender;
        }

        return result;
    }

    //Remove the ownership of a resource with given index, and notify all threads that wait for this resource.
    private void removeOwnership(int index)
    {
        ownership.remove(index);
        synchronized (resources[index])
        {
            resources[index].notify();
        }
    }

    //Remove all data where current thread is a key in hashmap.
    private void clearThread()
    {
        threadsPresent.remove(Thread.currentThread());
        threadsTime.remove(Thread.currentThread());
        operationHistory.remove(Thread.currentThread());
        lockedOn.remove(Thread.currentThread());
    }

    /* Mechanism that doesn't allow commits and findLock function
    * to run together at the same time. It however lets many commits to
    * run at the same time, as they don't interrupt each other. It is similar to one writer
    * and many readers problem. */
    private void waitForCommits()
    {
        secMutex.acquireUninterruptibly();

        if (currentCommitters.get() > 0)
        {
            isLockActive.getAndSet(true);
            secMutex.release();
            lockWait.acquireUninterruptibly();
            isLockActive.getAndSet(false);
        }

        else
        {
            allowLockCheck.getAndSet(true);
            secMutex.release();
        }
    }

    private void waitForLock()
    {
        secMutex.acquireUninterruptibly();

        if (!isLockActive.get() && !allowLockCheck.get())
        {
            currentCommitters.getAndIncrement();
            secMutex.release();
        }
        else
        {
            stoppedCommitters.getAndIncrement();
            secMutex.release();
            commitWait.acquireUninterruptibly();
        }
    }

    private void releaseCommits()
    {
        secMutex.acquireUninterruptibly();
        allowLockCheck.set(false);

        if (stoppedCommitters.get() > 0)
        {
            currentCommitters.set(stoppedCommitters.get());
            commitWait.release(stoppedCommitters.getAndSet(0));
        }

        secMutex.release();
    }

    private void releaseLocks()
    {
        secMutex.acquireUninterruptibly();
        currentCommitters.getAndDecrement();

        if (currentCommitters.get() == 0 && isLockActive.get())
        {
            allowLockCheck.getAndSet(true);
            lockWait.release();
        }

        secMutex.release();
    }
}
