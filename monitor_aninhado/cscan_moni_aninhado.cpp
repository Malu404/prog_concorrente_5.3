#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>
#include <chrono>
#include <random>
#include <sstream>
const int MAX_CYLINDER = 199;
std::mutex mtx; // Mutex pro print nao sair misturado pelos threads
// Monitor Disk_Transfer
//Problema de disco 5.3, essa solucao usa monitor aninhado conforme a secao 5.3.3 do livro
//Maria Luiza - 552655 e Lucas Pinheiro - 553311
class DiskTransfer {
private:
    std::mutex mtx;
public:
    void read(int id, int cyl) {
        {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "[User " << id << "] Reading from cylinder " << cyl << "\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200 + rand() % 300));
    }
};

// Monitor Disk_Access (como o antigo DiskScheduler)
class DiskAccess {
private:
    std::mutex mtx;
    std::condition_variable_any scan[2];
    std::map<int, std::vector<std::condition_variable_any*>> queue[2];
    int position = -1;
    int c = 0, n = 1;
    DiskTransfer& disk;
    //aqui é a função de espera, que adiciona o cilindro requisitado na fila de espera do cilindro atual ou do proximo cilindro
    void wait_request(int index, int cyl, std::unique_lock<std::mutex>& lock, std::condition_variable_any& cv) {
        queue[index][cyl].push_back(&cv);
        while (position != cyl)
            cv.wait(lock);
    }

    void signal_next() {//aqui faz o sinal para o proximo cilindro na fila de espera, seguindo a ordem da fila
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
    DiskAccess(DiskTransfer& d) : disk(d) {}

    void doio(int id, int cyl) {
         if(cyl < 0 || cyl > MAX_CYLINDER) {
            //obriga meu usuario a requisitar cilindros validos(0 a 199)
            //note que a flag so e chamada durante o acesso, mas a request ainda fica guardada para avaliacao, como voce pode ver que o [User 3] Requesting cylinder 420 e a mensagem de erro nao sao necessariamente seguidas. Concorrencia causa isso
            std::ostringstream oss;
            oss << "Invalid cylinder request: " << cyl << " Por parte do cliente "<< id <<". O cilindro deve estar entre 0 e " << MAX_CYLINDER << ".\n";
            throw std::out_of_range(oss.str());
        }
        std::condition_variable_any cv;
        std::unique_lock<std::mutex> lock(mtx);

        if (position == -1) {
            position = cyl;
        } else if (cyl > position) {
            wait_request(c, cyl, lock, cv);
        } else {
            wait_request(n, cyl, lock, cv);
        }

        // chamada aberta (open call) para o segundo monitor
        lock.unlock();
        disk.read(id, cyl);
        lock.lock();

        signal_next();
    }
};

// Simulador
void user_process(int id, int cyl, DiskAccess& access) {//aqui simula os usuarios requisitando cilindros
    std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 1000));
    {   
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "[User " << id << "] Requesting cylinder " << cyl << "\n";
    }
    try { access.doio(id, cyl);
    {   
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "[User " << id << "] Finished operation on cylinder " << cyl << "\n";
    }

    } catch (const std::out_of_range& e) {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "[User " << id << "] Pedido negado: " << e.what();
    }
}//infelizmente meus std::cout nao saem na ordem que eu queria, mas isso é devido a concorrencia, o que é esperado
//nao consegui arrumar o print, que fica saindo no print a conclusao de um cilindro misturado a leitura de outro, culpa da concorrencia

int main() {
    srand((unsigned)time(0));
    DiskTransfer disk;
    DiskAccess access(disk);

    std::vector<std::thread> users;
    std::vector<int> requests = {50, 10, 170, 3, 75, 90, 8, 110, 9999, 420, 150, 30, 180, 200}; // Inclui cilindros inválidos para testar o tratamento de erros

    for (int i = 0; i < requests.size(); ++i) {
        users.emplace_back(user_process, i + 1, requests[i], std::ref(access));
    }

    for (auto& t : users) {
        t.join();
    }

    std::cout << "All operations completed.\n";
    return 0;
}
