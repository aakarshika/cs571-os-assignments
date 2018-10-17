/*
 * Copyright (c) 2001, 2002, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver code for whale mating problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define NMATING 10


static struct semaphore *male_sem;
static struct semaphore *female_sem;
static struct semaphore *mm_sem;

static struct cv *male_cv;
static struct cv *female_cv;
static struct cv *mm_cv;

static struct lock *mating_lock;

static int matched;

static
void
male(void *p, unsigned long which)
{
	(void)p;
	kprintf("Male whale #%ld starting\n", which);

	// 
	
	//lock_acquire(mating_lock);	
	if(female_sem->sem_count == 0 || mm_sem->sem_count  == 0)
  	{
    	//No female or matchmaker
        	V(male_sem);
	        cv_wait(male_cv,mating_lock);
		kprintf("Male %ld waiting...\n",which);
        	
	}
	else
	{
         //Female and matchmaker also present
        	 P(female_sem);
                 P(mm_sem);
                 cv_signal(female_cv, mating_lock);
                 cv_signal(mm_cv,mating_lock);
		kprintf("\tMating male %ld\n",which);
		kprintf("\tMatched: %d pairs.\n", ++matched);
    	 }
        // lock_release(mating_lock);
//	kprintf("male whale %ld ending\n", which);
}

static
void
female(void *p, unsigned long which)
{
	(void)p;
	kprintf("Female whale #%ld starting\n", which);

        //lock_acquire(mating_lock);

	if(male_sem->sem_count == 0 || mm_sem->sem_count  == 0)
        {
        //No male or matchmaker

 		V(female_sem);
                cv_wait(female_cv,mating_lock);
                kprintf("Female %ld waiting...\n",which);

        }
        else
        {
         //Decrementing  female and matchmaker with current male
         
		 P(male_sem);
                 P(mm_sem);
                 cv_signal(male_cv,mating_lock);
                 cv_signal(mm_cv,mating_lock);
                kprintf("\tMating female %ld\n",which);
		kprintf("\tMatched: %d pairs.\n", ++matched);

         }
        //lock_release(mating_lock);

}

static
void
matchmaker(void *p, unsigned long which)
{
	(void)p;
	kprintf("Matchmaker whale #%ld starting\n", which);

        //lock_acquire(mating_lock);
	if(male_sem->sem_count == 0 || female_sem->sem_count  == 0)
        {
        //Matchmaker is alone

                V(mm_sem);
                cv_wait(mm_cv,mating_lock);
                kprintf("Matchmaker %ld waiting...\n",which);

        }
        else
        {
         // Male Female also present

                 P(male_sem);
                 P(female_sem);
                 cv_signal(male_cv,mating_lock);
                 cv_signal(female_cv,mating_lock);
		kprintf("\tMatchmaker %ld successful.\n", which);
        	kprintf("\tMatched: %d pairs.\n", ++matched);

	 }
        //lock_release(mating_lock);


}


// Change this function as necessary
int
whalemating(int nargs, char **args)
{

	male_sem = sem_create("Male whales",0);        
	female_sem = sem_create("Female whales",0);
	mm_sem = sem_create("Matchmaker whales",0);
        
	
	mating_lock = lock_create("Mating lock");
	
	male_cv = cv_create("Male");
        female_cv = cv_create("Female");
        mm_cv = cv_create("Matchmaker");




	int i, j, err=0;

	(void)nargs;
	(void)args;

	for (i = 0; i < 3; i++) {
		for (j = 0; j < NMATING; j++) {
			switch(i) {
			    case 0:
				err = thread_fork("Male Whale Thread",
						  NULL, male, NULL, j);
				break;
			    case 1:
				err = thread_fork("Female Whale Thread",
						  NULL, female, NULL, j);
				break;
			    case 2:
				err = thread_fork("Matchmaker Whale Thread",
						  NULL, matchmaker, NULL, j);
				break;
			}
			if (err) {
				panic("whalemating: thread_fork failed: %s)\n",
				      strerror(err));
			}
		}
	}

	return 0;
}

