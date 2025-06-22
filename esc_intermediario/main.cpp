#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <set>
#include <string>
#include <chrono>
#include <algorithm>

using namespace std;
// SImulação 
bool debug_mode = false;
mutex cout_mutex;

void log_message(const string& message) {
    if (debug_mode) {
        lock_guard<mutex> lock(cout_mutex);
        cout << message << endl;
    }
}

struct argType {
    int userId;
    string data;
};

struct resultType {
    bool success;
    string message;
};

class DiskInterface {
private:
    // Mutex da classe DiskInterface, usado para proteger o acesso aos recursos. 
    // Garante que apenas uma thread acesse.
    mutex mtx;

    int position; // Estado do cilindro (-1: ocioso, -2: Não iniciado, >= 0: reprensenta o cilindro atual)
    int c, n; // Direções de varredura, usadas para inverter a direção do scan
    int args , results; // Flags usadas pelo buffer para indicar o estado de arg_area e result_area
    
    // Implementação do scan[2]
    // fila_scan[0] armazena requisições na direção C (crescente)
    // fila_scan[1] armazena requisições na direção N (decrescente)
    // set mantém a ordem do rank dos cilindros
    set<int> fila_scan[2];
    condition_variable scan[2];
    
    argType arg_area;
    resultType result_area;

    // área de espara para: driver (receber argumentos), usuário (receber resultados) e driver (sobreescrever apenas quando o usuário já tiver lido os resultados)
    condition_variable args_stored, results_stored, results_retrieved;

public:
    DiskInterface() : position(-2), c(0), n(1), args(0), results(0) {}

    int get_position() const {
        return position;
    }

    void use_disk(int cyl, argType transfer_params, resultType& result_params) {
        // Garante a trava no mutex após a thread entrar na função
        unique_lock<mutex> lock(mtx);
        log_message("[USUÁRIO " + to_string(transfer_params.userId) + "] Tentando acessar cilindro " + to_string(cyl) + ". Posição atual do cabeçote: " + to_string(position));

        if (position == -1 || position == -2) {
            position = cyl;
            log_message("[USUÁRIO " + to_string(transfer_params.userId) + "] Disco estava livre. Assumiu o cilindro " + to_string(cyl) + " diretamente.");
        } else if (position!=-1 && cyl >= position) {
            fila_scan[c].insert(cyl);
            log_message("[USUÁRIO " + to_string(transfer_params.userId) + "] Entrou na fila de espera (direção " + to_string(c) + ") para o cilindro " + to_string(cyl) + ". Aguardando...");
            scan[c].wait(lock, [&]{ return position == cyl; });
        } else {
            fila_scan[n].insert(cyl);
            log_message("[USUÁRIO " + to_string(transfer_params.userId) + "] Entrou na fila de espera (direção " + to_string(n) + ") para o cilindro " + to_string(cyl) + ". Aguardando...");
            scan[n].wait(lock, [&]{ return position == cyl; });
        }
        log_message("[USUÁRIO " + to_string(transfer_params.userId) + "] Acessando cilindro " + to_string(position) + ". Enviando argumentos.");
        arg_area = transfer_params;
        args = args + 1;
        args_stored.notify_one();
        while (results == 0) {
            results_stored.wait(lock, [&]{ return results > 0; });
        }
        result_params = result_area;
        results = results - 1;
        results_retrieved.notify_one(); 
        log_message("[USUÁRIO " + to_string(transfer_params.userId) + "] Resultados recebidos. Requisição concluída.");
    }

    void get_next_request( argType& transfer_params) {
        unique_lock<mutex> lock(mtx);
        if(position == -2) position = -1; 
        log_message("[DRIVER]   Pronto para a próxima requisição. Cabeçote na posição: " + to_string(position));

        if (!fila_scan[c].empty()) {
            position = *fila_scan[c].begin();
            fila_scan[c].erase(fila_scan[c].begin());
        } else if (fila_scan[c].empty() && !fila_scan[n].empty()) {
            log_message("[DRIVER]   Fim da varredura na direção " + to_string(c) + ". Invertendo para a direção " + to_string(n) + ".");
            swap(c, n);
            position = *fila_scan[c].begin();
            fila_scan[c].erase(fila_scan[c].begin());
        } else {
            log_message("[DRIVER]   Não há requisições. Disco ficará ocioso.");
            position = -1;
        }
        log_message("[DRIVER]   Selecionou a próxima requisição. Movendo cabeçote para o cilindro " + to_string(position) + ".");
        scan[c].notify_all();
        while (args == 0) {
            args_stored.wait(lock, [&]{ return args > 0; });
        }
        transfer_params = arg_area;
        args = args - 1;
        log_message("[DRIVER]   Argumentos recebidos do usuário " + to_string(transfer_params.userId) + " para o cilindro " + to_string(position) + ".");
    }

    void finished_transfer(resultType result_vals) {
        unique_lock<mutex> lock(mtx);
        log_message("[DRIVER]   Transferência de dados concluída para o cilindro " + to_string(position) + ". Enviando resultados.");
        
        result_area = result_vals;
        results = results + 1;
        
        results_stored.notify_one();
        
        while (results > 0) {
            results_retrieved.wait(lock, [&]{ return results == 0; });
        }
        log_message("[DRIVER]   Usuário confirmou o recebimento dos resultados.");
    }
};

void user_process(int id, int cylinder, DiskInterface& disk) {
    argType args = {id, "dados do usuario " + to_string(id)};
    resultType res;

    // Simula um tempo antes de fazer a requisição
    this_thread::sleep_for(chrono::milliseconds(100 * id));

    disk.use_disk(cylinder, args, res);
}

void driver_process(int total_requests, DiskInterface& disk) {
    log_message("[DRIVER]   Processo do Driver iniciado.");
    for (int i = 0; i < total_requests; ++i) {
        argType next_request = {-1, ""};
        
        disk.get_next_request(next_request);

        if (next_request.userId != -1) {
            log_message("[DRIVER]   >>> INICIANDO SERVIÇO para o usuário " + to_string(next_request.userId) + " no cilindro " + to_string(disk.get_position()) + " <<<");
            
            // Simula o tempo de I/O do disco
            this_thread::sleep_for(chrono::milliseconds(500));
            
            log_message("[DRIVER]   >>> SERVIÇO CONCLUÍDO para o usuário " + to_string(next_request.userId) + " <<<");

            resultType result = {true, "Operacao bem sucedida"};
            disk.finished_transfer(result);
        } else {
            // Se não encontrou requisição, espera um pouco e tenta de novo.
            // Isso evita que o driver saia do loop se as requisições demorarem a chegar.
            i--; 
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
    log_message("[DRIVER]   Todas as " + to_string(total_requests) + " requisições foram atendidas. Driver encerrando.");
}


int main() {
    char debug_choice;
    cout << "Deseja ativar o modo de depuracao (transparencia)? (s/n): ";
    cin >> debug_choice;
    if (debug_choice == 's' || debug_choice == 'S') {
        debug_mode = true;
    }

    cout << "\n--- Iniciando a Simulacao do Escalonador de Disco---\n" << endl;

    DiskInterface disk_monitor;

    // Lista de requisições (id do usuário, cilindro)
    vector<pair<int, int>> requests = {
        {1, 98}, {2, 183}, {3, 37}, {4, 122}, {5, 14}, {6, 124}, {7, 65}, {8, 67}
    };
    int num_requests = requests.size();

    // Cria e inicia a thread do driver
    thread driver_thread(driver_process, num_requests, ref(disk_monitor));

    // Cria e inicia as threads de usuário
    vector<thread> user_threads;
    for (const auto& req : requests) {
        user_threads.emplace_back(user_process, req.first, req.second, ref(disk_monitor));
    }

    for (auto& t : user_threads) {
        t.join();
    }

    driver_thread.join();

    return 0;
}