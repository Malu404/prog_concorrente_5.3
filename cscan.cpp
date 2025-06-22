#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>
#include <algorithm>
#include <chrono>

const int MAX_CYLINDER = 199;

class DiskScheduler {
private:
    std::mutex mtx;
    std::condition_variable_any scan[2]; // scan[0] -> C, scan[1] -> N
    std::map<int, std::vector<std::condition_variable_any*>> queue[2]; // 0 = C, 1 = N
    int position = -1;
    int c = 0; // current scan index
    int n = 1; // next scan index

    void wait_request(int index, int cyl, std::unique_lock<std::mutex>& lock, std::condition_variable_any& cv) {
        queue[index][cyl].push_back(&cv);
        while (position != cyl) {
            cv.wait(lock);
        }
    }

    void signal_next() {
        if (!queue[c].empty()) {
            int next_cyl = queue[c].begin()->first;
            position = next_cyl;
            auto& list = queue[c][next_cyl];
            auto* cv = list.front();
            list.erase(list.begin());
            if (list.empty()) queue[c].erase(next_cyl);
            cv->notify_one();
        } else if (!queue[n].empty()) {
            std::swap(c, n);
            signal_next();
        } else {
            position = -1;
        }
    }

public:
    void request(int cyl) {
        std::unique_lock<std::mutex> lock(mtx);
        std::condition_variable_any cv;

        if (position == -1) {
            position = cyl;
            return;
        }

        if (cyl > position) {
            wait_request(c, cyl, lock, cv);
        } else {
            wait_request(n, cyl, lock, cv);
        }
    }

    void release() {
        std::unique_lock<std::mutex> lock(mtx);
        signal_next();
    }

    int current_position() {
        std::lock_guard<std::mutex> lock(mtx);
        return position;
    }
};

DiskScheduler scheduler;

void user_process(int id, int cyl) {
    std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 1000)); // simulate arrival time
    std::cout << "[User " << id << "] Requesting cylinder " << cyl << "\n";
    scheduler.request(cyl);

    // Simulate disk access
    std::cout << "[User " << id << "] Accessing cylinder " << cyl << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(300 + rand() % 200));

    scheduler.release();
    std::cout << "[User " << id << "] Released cylinder " << cyl << "\n";
}

int main() {
    std::vector<std::thread> users;
    std::vector<int> requests = {55, 58, 39, 18, 90, 160, 150, 38, 184}; // sample requests

    for (int i = 0; i < requests.size(); ++i) {
        users.emplace_back(user_process, i + 1, requests[i]);
    }

    for (auto& th : users) {
        th.join();
    }

    std::cout << "All disk requests completed.\n";
    return 0;
}
