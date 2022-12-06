#include "alarmer.h"
#include <signal.h>
#include <vector>
#include <mutex>
#include <algorithm>
#include "main.h"
#include "core.h"

using std::find;
using std::vector;
using std::mutex;
typedef std::lock_guard<mutex> lock;

static vector<pthread_t> thrs;
static mutex mtx;

void alarm_thread(pthread_t thr)
{
	lock lck(mtx);
	pthread_kill(thr, SIGINT);
	if (find(thrs.begin(), thrs.end(), thr) == thrs.end())
		thrs.push_back(thr);
	alarm(1);
}

void alarm_stop(pthread_t thr)
{
	lock lck(mtx);
	auto p = find(thrs.begin(), thrs.end(), thr);
	if (p != thrs.end())
		thrs.erase(p);
	if (!thrs.empty())
		alarm(1);
}

static void on_alarm()
{
	if (!mtx.try_lock())
		return;
	for (pthread_t thr : thrs)
		pthread_kill(thr, SIGINT);
	if (!thrs.empty())
		alarm(1);
	mtx.unlock();
}

static void sighandler(int signum)
{
	if (signum == SIGALRM)
		on_alarm();
	else if (signum == SIGTERM) {
		prog_status = ProgramStatus::exit;
		core->notify();
	} else if (signum == SIGHUP) {
		prog_status = ProgramStatus::reload;
		core->notify();
	}
}

void init_signals()
{
	struct sigaction sact;
	memset(&sact, 0, sizeof(sact));
	sact.sa_handler = sighandler;
	sigaction(SIGHUP, &sact, NULL);
	sigaction(SIGINT, &sact, NULL);
	sigaction(SIGALRM, &sact, NULL);
	sigaction(SIGTERM, &sact, NULL);
	signal(SIGPIPE, SIG_IGN);
}
