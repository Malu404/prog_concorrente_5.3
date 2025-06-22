#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>
#include <algorithm>
#include <chrono>
#include <sstream>
//Problema de disco 5.3, essa solucao usa monitor separado conforme a secao 5.3.1 do livro
//Maria Luiza - 552655 e Lucas Pinheiro - 553311
const int MAX_CYLINDER = 199;
std::mutex print_mtx;// como acabei de descobrir que o std::cout nao e thread-safe, vou usar um mutex pro print parar de ficar todo feio picotado pelas threads.
class DiskScheduler {//implementacao do monitor separado
    //aqui o monitor separado e implementado como uma classe que tem mutex, condicao e fila de espera
    //para cada cilindro, e a posicao atual do cilindro
    //o monitor separado permite que cada cilindro tenha sua propria fila de espera
    //e permite que os usuarios esperem pelo cilindro sem bloquear outros cilindros
private:
    std::mutex mtx;
    std::condition_variable_any scan[2]; // scan[0] -> C, scan[1] -> N
    std::map<int, std::vector<std::condition_variable_any*>> queue[2]; // 0 = C, 1 = N
    int position = -1;
    int c = 0; // index atual do scan
    int n = 1; // proximo index do scan

    void wait_request(int index, int cyl, std::unique_lock<std::mutex>& lock, std::condition_variable_any& cv) {
        queue[index][cyl].push_back(&cv);//fila de espera dos usuarios para o usar o cilindro
        while (position != cyl){
            cv.wait(lock);//espera rodar o cilindro ate chegar no cilindro requisitado
        }
    }

    void signal_next() {//aqui faz o sinal para o proximo cilindro
        if (!queue[c].empty()) {
            int next_cyl = queue[c].begin()->first;//pego o primeiro cilindro da fila de espera
            position = next_cyl;//atualizo a posicao do cilindro atual
            auto& list = queue[c][next_cyl];
            auto* cv = list.front();
            list.erase(list.begin());
            if (list.empty()) queue[c].erase(next_cyl);
            cv->notify_one();
        } else if (!queue[n].empty()) {
            std::swap(c, n);
            signal_next();
        } else {
            position = -1;//se nao tiver mais cilindros na fila de espera, a posicao do cilindro atual fica -1, indica vazio
        }
    }

public://aqui ficam os metodos publicos que os usuarios podem chamar para requisitar e liberar cilindros
    void request(int cyl, int id) {
        if(cyl < 0 || cyl > MAX_CYLINDER) {
            //obriga meu usuario a requisitar cilindros validos(0 a 199)
            //note que a flag so e chamada durante o acesso, mas a request ainda fica guardada para avaliacao, como voce pode ver que o [User 3] Requesting cylinder 420 e a mensagem de erro nao sao necessariamente seguidas. Concorrencia causa isso
            std::ostringstream oss;
            oss << "Invalid cylinder request: " << cyl << " Por parte do cliente "<< id <<".Deve estar entre 0 e " << MAX_CYLINDER << ".\n";
            throw std::out_of_range(oss.str());
        }
        std::unique_lock<std::mutex> lock(mtx);
        std::condition_variable_any cv;
        
        //se a posicao estiver -1(vazio), atualiza ela para o cilindro requisitado
        if (position == -1) {
            position = cyl;
            return;
        }
        //se o cilindro requisitado estiver depois da posicao atual, adiciona na fila de espera do cilindro atual
        if (cyl > position) {
            wait_request(c, cyl, lock, cv);
        } else {//se estiver antes, adiciona na fila de espera da proxima volta completa do cilindro
            wait_request(n, cyl, lock, cv);
        }
    }

    void release() {//libera o cilindro atual, sinalizando o proximo cilindro na fila de espera
        std::unique_lock<std::mutex> lock(mtx);
        signal_next();
    }

    int current_position() {//retorna a posicao atual do cilindro
        std::lock_guard<std::mutex> lock(mtx);
        return position;
    }
};

DiskScheduler scheduler;
 

void user_process(int id, int cyl) {
    //simulador de pedido de acesso ao disco
    std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 1000)); 
    // randomizador para fingir que um usuario real ta pedindo acesso
    //note que e randomizado pois programacao concorrente nao e deterministica em termos de quem pediu acesso primeiro
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << "[User " << id << "] Requesting cylinder " << cyl << "\n";
    }
    try {scheduler.request(cyl,id);}
    catch (const std::out_of_range& e) {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << "[User " << id << "] Pedido negado: " << e.what();
        return; // se o cilindro requisitado for invalido, sai do processo
    }

    //simulador de tempo gasto acessando o cilindro
    {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << "[User " << id << "] Accessing cylinder " << cyl << "\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300 + rand() % 200));
    //depois de um tempo aleatorio X, o usuario que pediu o acesso ao cilindro libera o cilindro
    scheduler.release();//chama meu monitor para liberar o cilindro requisitado
    { 
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cout << "[User " << id << "] Released cylinder " << cyl << "\n";
    }
    
}

int main() {
    std::vector<std::thread> users;//minha thread de usuarios, pra cada um poder ser identificado com seu cilindro x id
    std::vector<int> requests = {69, 100, 420, 3, 7, 12, 13, 8, 51}; // batch de simulacao cilindros requisitados
    for (int i = 0; i < requests.size(); ++i) {
        users.emplace_back(user_process, i + 1, requests[i]);//coloca cada usuario na thread, com seu id e cilindro requisitado
    }

    for (auto& th : users) {
        th.join();//espera todas as threads terminarem antes de continuar
    }

    std::cout << "All disk requests completed.\n";// mensagem final de que todos os pedidos foram completados
    return 0;
}
