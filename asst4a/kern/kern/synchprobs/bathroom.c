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

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define NPEOPLE 20


static volatile unsigned int boy_count;
static volatile unsigned int girl_count;

static volatile unsigned int current_service;
static volatile unsigned int next_service;

static struct semaphore *bath_sem;
static struct semaphore *completed_sem;

//static struct semaphore *boy_sem;
//static struct semaphore *girl_sem;

static struct lock      *bathroom_lock;

static struct cv        *boy_cv;
static struct cv        *girl_cv;

static
void
shower()
{
	// The thread enjoys a refreshing shower!
        clocksleep(1);
}

static
void
boy(void *p, unsigned long which)
{
	(void)p;
	kprintf("boy #%ld starting\n", which);


	

	lock_acquire(bathroom_lock);
	boy_count++;

	if(current_service==0) //service 0 means unoccupied
	{
		if(boy_count>girl_count)
			current_service=1; //service 1 - serving boys
		else
			current_service=2; // service 2 -  serving girls
	}
	kprintf("boy #%ld starting\n", which);

	//service is the control variable for the gender entering the bathroom
	//next_service holds the next gender to be serviced. Helps for fair usage policy.
	
	
	while(current_service==2 || bath_sem->sem_count==0 || next_service==2)
		cv_wait(boy_cv,bathroom_lock); //wait and serve boys until service change
	
	P(bath_sem);//bathroom entered
	boy_count--;
	lock_release(bathroom_lock);
	
	


	kprintf("boy #%ld entering bathroom...\n", which);
	//Use Bathroom
	shower();
	kprintf("boy #%ld leaving bathroom\n", which);


	lock_acquire(bathroom_lock);
	V(bath_sem);//bathroom exited
	
	if(girl_count!=0)
	{
		next_service=2; //change service if girl waiting
	}
	else
	{
		if(boy_count==0)
			next_service=0; //Unoccupied when no one waiting
		else
		{
			next_service=1; 
			cv_broadcast(boy_cv, bathroom_lock); //Broadcast next boy
		}
	}
	if(bath_sem->sem_count==3)
	{
		current_service=next_service;
		if(current_service==1)
			cv_broadcast(boy_cv,bathroom_lock); // broadcast next boy
		else if(current_service==2)
			cv_broadcast(girl_cv,bathroom_lock); // broadcast next girl
	}

	lock_release(bathroom_lock);
	V(completed_sem); // To count total number of people taken bath.

}

static
void
girl(void *p, unsigned long which)
{
	(void)p;
	kprintf("girl #%ld starting\n", which);


        lock_acquire(bathroom_lock);
        girl_count++;

        if(current_service==0)
        {
                if(boy_count>girl_count)
                        current_service=1;
                else
                        current_service=2;
        }
        kprintf("girl #%ld starting\n", which);
        

        while(current_service==1 || bath_sem->sem_count==0 || next_service==1)
                cv_wait(girl_cv,bathroom_lock);
        P(bath_sem);
        girl_count--;
        lock_release(bathroom_lock);

        


        kprintf("girl #%ld entering bathroom...\n", which);
        shower();
        kprintf("girl #%ld leaving bathroom\n", which);


        lock_acquire(bathroom_lock);
        V(bath_sem);
        if(boy_count!=0)
        {
                next_service=1;
        }
        else
        {
                if(girl_count==0)
                        next_service=0;
                else
                {
                        next_service=2;
                        cv_broadcast(girl_cv, bathroom_lock);
                }
        }
        if(bath_sem->sem_count==3)
        {
                current_service=next_service;
                if(current_service==1)
                        cv_broadcast(boy_cv,bathroom_lock);
                else if(current_service==2)
                        cv_broadcast(girl_cv,bathroom_lock);
        }

        lock_release(bathroom_lock);
        V(completed_sem);
	
	

}

// Change this function as necessary
int
bathroom(int nargs, char **args)
{

	bath_sem = sem_create("Bathrooms busy", 3);
	completed_sem = sem_create("Bathed Students", 0);
	bathroom_lock = lock_create("Bathroom Lock");

	boy_cv = cv_create("Boys CV");
	girl_cv = cv_create("Girls CV");	

int i, err=0;

	(void)nargs;
	(void)args;


	for (i = 0; i < NPEOPLE; i++) {
		switch(i % 2) {
			case 0:
			err = thread_fork("Boy Thread", NULL,
					  boy, NULL, i);
			break;
			case 1:
			err = thread_fork("Girl Thread", NULL,
					  girl, NULL, i);
			break;
		}
		if (err) {
			panic("bathroom: thread_fork failed: %s)\n",
				strerror(err));
		}
	}

	for(i=0;i<NPEOPLE;i++)
		P(completed_sem);

	return 0;
}

