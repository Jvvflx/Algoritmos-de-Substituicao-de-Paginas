#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#define TAMANHO_MEMORIA_CACHE 64
#define TAMANHO_MEMORIA_DISCO 100000000
#define JANELA_WORKING_SET 3  // Tamanho da janela do working set

typedef struct pagina {
    int timestamp_ultima_ref;   // Timestamp da última referência (essencial para WSClock)
    int valida;                // Bit de validade
    int R;                     // Bit de referência
    int M;                     // Bit de modificação
    int prot;                  // Bit de proteção
    int indice_pagina;         // Índice da página virtual (simplificado)
    struct pagina *prox;       // Próxima página no relógio
    struct pagina *ante;       // Página anterior no relógio
} Pagina;

typedef struct relogio {
    Pagina* ponteiro_relogio;  // Ponteiro atual do relógio
    int qtd_paginas;
    int tempo_virtual;
} Relogio;

typedef struct fila {
    Pagina* lista; 
    int qtd_paginas;
    int pagina_atual;
    int tempo_virtual;
} Fila;

typedef struct operacao{
    char tipo;
    int indice;
    int prot;
    char mensagem[3];
}Operacao;

typedef struct processo{
    int indice_processo;
    int qtd_paginas;
    int operacoes_rest;
    int ausencia_leve;
    int ausencia_completa;
    int total_operacoes;
    Operacao* sequencia_operacoes;
    int ultimo_indice;
}Processo;


// Estrutura para gerenciar a cache de forma mais eficiente
typedef struct cache_un {
    int indice_virtual;
    int indice_real_disco;
    int processo_dono;
    int valida;
    int timestamp_uso;
    char mensagem[3];
    int prot;
} CacheUn;

// Variáveis globais para cache
CacheUn cache_estruturada[TAMANHO_MEMORIA_CACHE];
int cache_ocupada = 0;
int cache_lru_counter = 0;

// Variáveis globais
Relogio listaPaginas;
Processo* listaProcessos;
Fila filaAgendamento;
Operacao* disco;
int total_paginas = 0;
int max_processos = 0;
int limite_paginas = 0;
int limite_uso_cpu = 0;
int tempo_virtual_atual = 0;
int processo_atual = 0;
int operacoes_global = 0;
int operacoes_totais = 0;
int offset_disco = 0;
int sistema_ativo = 1;

pthread_mutex_t mutex_disco;
pthread_mutex_t mutex_fila;

// Variáveis de comparação
int ausencia_leve = 0;
int ausencia_completa = 0;

// Protótipos das funções
void* escritaDisco(void* args);
void travamentoDuasEtapas();
void destravamentoDuasEtapas();
void substituirPagina(Pagina *nova_pagina);
void trocaPagina(Pagina *antiga, Pagina *nova);
void agendaEscrita(Pagina *page);
int esta_no_working_set(Pagina *page);
void adicionar_pagina_Relogio(Operacao dados);
void adicionar_pagina_Fila(Pagina *page);
void imprimir_estado_sistema();
void simular_referencias(int processo);
Pagina* criar_pagina(Operacao dados);
void leituraArquivo();
void referenciar_pagina(Operacao dados);
void imprimir_estado_cache();
int buscar_na_cache(int indice_virtual);
int carregar_pagina_para_cache(int indice_virtual);
void inicializar_cache();
int encontrar_slot_cache();
void escritaArquivo();

void* escritaDisco(void *args){

    while(sistema_ativo){
        if(tempo_virtual_atual % limite_uso_cpu == 0 && tempo_virtual_atual > 0){
            travamentoDuasEtapas();

            if(filaAgendamento.qtd_paginas > 0){

                for(int i = filaAgendamento.pagina_atual; i < filaAgendamento.qtd_paginas; filaAgendamento.pagina_atual++){
                    int indice_pagina = filaAgendamento.lista[i].indice_pagina;
                    int indice_memoria = indice_pagina - 100;

                    disco[indice_memoria].mensagem[0] = cache_estruturada[indice_memoria].mensagem[0];
                    disco[indice_memoria].mensagem[1] = cache_estruturada[indice_memoria].mensagem[1];
                    
                    filaAgendamento.lista[i].M = 0;
                    filaAgendamento.qtd_paginas--;
                }

            }

            destravamentoDuasEtapas();
            printf("Escrita em disco concluída.\n");
        }
        
        usleep(100000);
    }

}

void travamentoDuasEtapas(){

    // Verifica se ambos estão livres para poder realizar a trava em ambos
    if(mutex_fila.__align){
        pthread_mutex_trylock(&mutex_fila);
        if(mutex_disco.__align){
            pthread_mutex_trylock(&mutex_disco);
            return;
        }
        else{
            pthread_mutex_unlock(&mutex_fila);
        }
    }
    
    // Se chegou aqui, não conseguiu travar ambos
    // Espera um pouco e tenta novamente
    usleep(1000);
    travamentoDuasEtapas();

}

void destravamentoDuasEtapas(){

    // Destrava ambos
    pthread_mutex_unlock(&mutex_disco);
    pthread_mutex_unlock(&mutex_fila);

}

// Cria uma nova página
Pagina* criar_pagina(Operacao dados) {
    Pagina* nova = (Pagina*)malloc(sizeof(Pagina));
    if (!nova) return NULL;
    
    nova->indice_pagina = dados.indice;
    nova->timestamp_ultima_ref = tempo_virtual_atual;
    nova->valida = 1;
    nova->R = 1; // Página recém carregada foi referenciada
    nova->M = 0; // Página limpa inicialmente
    nova->prot = dados.prot;
    nova->prox = nova; // Inicialmente aponta para si mesmo
    nova->ante = nova;
    
    return nova;
}

// Adiciona uma página ao sistema (estrutura circular)
void adicionar_pagina_Relogio(Operacao dados) {
    
    if(listaPaginas.qtd_paginas >= limite_paginas) {
        printf("Limite de páginas atingido, substituindo...\n");
        Pagina* nova = criar_pagina(dados);
        if(nova) {
            substituirPagina(nova);
        }
        return;
    }
    
    Pagina* nova = criar_pagina(dados);
    if (!nova) {
        printf("Erro: não foi possível alocar memória para página\n");
        return;
    }
    
    if (listaPaginas.qtd_paginas == 0) {
        listaPaginas.ponteiro_relogio = nova;
    } 
    else {
        Pagina* ultimo = listaPaginas.ponteiro_relogio->ante;
        
        nova->prox = listaPaginas.ponteiro_relogio;
        nova->ante = ultimo;
        ultimo->prox = nova;
        listaPaginas.ponteiro_relogio->ante = nova;
    }
    
    listaPaginas.qtd_paginas++;
    printf("Página %d adicionada ao sistema. Total: %d páginas no relógio\n", 
           dados.indice, listaPaginas.qtd_paginas);
    
}

// Adiciona uma página ao sistema (estrutura circular)
void adicionar_pagina_Fila(Pagina *page) {
    
    
    filaAgendamento.lista[filaAgendamento.qtd_paginas] = *page;
    filaAgendamento.qtd_paginas++;
    printf("Página %d adicionada ao sistema. Total: %d páginas na fila\n", 
           page->indice_pagina, filaAgendamento.qtd_paginas);
    
}

// Implementação do WSClock
void substituirPagina(Pagina *nova_pagina) {
    if (listaPaginas.qtd_paginas == 0) {
        printf("Sistema vazio, adicionando primeira página\n");
        adicionar_pagina_Relogio((Operacao){nova_pagina->R ? 'R' : 'M', nova_pagina->indice_pagina, nova_pagina->prot, ""});
        return;
    }
    
    Pagina* atual = listaPaginas.ponteiro_relogio;
    int volta_completa = 0;
    Pagina* inicio_busca = atual;
    
    printf("\n--- Iniciando substituição WSClock ---\n");
    
    while(1) {
        // Verifica se deu volta completa sem encontrar página
        if(volta_completa && atual == inicio_busca) {
            // Se todas as páginas estão no working set, substitui a atual mesmo assim
            if(atual->M == 1) {
                agendaEscrita(atual);
            }
            trocaPagina(atual, nova_pagina);
            break;
        }
        
        // Bit R está setado
        if(atual->R == 1) {
            atual->R = 0;  // Limpa o bit R
            atual->timestamp_ultima_ref = tempo_virtual_atual;  // Atualiza timestamp
        }
        // Bit R não está setado
        else {
            // Verifica se a página está fora do working set
            if(!esta_no_working_set(atual)) {
                // Página fora do working set, pode ser substituída
                if(atual->M == 1) {
                    // Página suja, agenda para escrita
                    agendaEscrita(atual);
                    atual->M = 0;  // Limpa bit M após agendar escrita
                    // Continua procurando por uma página limpa fora do working set
                } else {
                    // Página limpa e fora do working set - substitui
                    trocaPagina(atual, nova_pagina);
                    break;
                }
            }
        }
        
        // Avança para próxima página
        atual = atual->prox;
        if(atual == inicio_busca) {
            volta_completa = 1;
        }
    }
    
    // Atualiza ponteiro do relógio para próxima posição
    listaPaginas.ponteiro_relogio = nova_pagina->prox;
    
    printf("--- Substituição WSClock concluída ---\n\n");
}

// Função para verificar se a página está no working set
int esta_no_working_set(Pagina *page) {
    int diferenca_tempo = tempo_virtual_atual - page->timestamp_ultima_ref;
    return diferenca_tempo <= JANELA_WORKING_SET;
}

// Função para troca uma página antiga por uma nova
void trocaPagina(Pagina *antiga, Pagina *nova) {
    // CORREÇÃO: Verifica se há apenas uma página no sistema
    if(listaPaginas.qtd_paginas == 1) {
        listaPaginas.ponteiro_relogio = nova;
        nova->prox = nova;
        nova->ante = nova;
    } 
    else {
        // Copia dados importantes
        nova->prox = antiga->prox;
        nova->ante = antiga->ante;

        // Atualiza ponteiros das páginas vizinhas
        antiga->ante->prox = nova;
        antiga->prox->ante = nova;
        
        // Atualiza ponteiro do relógio se necessário
        if(listaPaginas.ponteiro_relogio == antiga) {
            listaPaginas.ponteiro_relogio = nova;
        }
    }
    
    // Configura nova página
    nova->R = 1;
    nova->timestamp_ultima_ref = tempo_virtual_atual;
    
    printf("Página %d substituída por página %d\n", antiga->indice_pagina, nova->indice_pagina);
    
    // Libera página antiga
    free(antiga);
}

// Função para agendar uma página suja para escrita no disco
void agendaEscrita(Pagina *page) {

    printf("Agendando escrita da página %d para disco\n", page->indice_pagina);

    // // Adiciona página na fila de escrita
    // for(int i = 0; i < filaAgendamento.qtd_paginas; i++){
    //     if(page->indice_pagina == filaAgendamento.lista[i].indice_pagina){
    //         printf("Página %d já está na fila para escrita em disco\n", page->indice_pagina);
    //         return;
    //     }
    // }

    // pthread_mutex_lock(&mutex_fila);
    // adicionar_pagina_Fila(page);
    // filaAgendamento.tempo_virtual = tempo_virtual_atual;
    // pthread_mutex_unlock(&mutex_fila);

    printf("Página %d adicionada na fila para escrita no disco\n", page->indice_pagina);
    
}


// Função para simular referência a uma página
void referenciar_pagina(Operacao dados) {
    
    // Procura a página na TLB (lista de páginas do relógio)
    if (listaPaginas.qtd_paginas > 0) {
        Pagina* atual = listaPaginas.ponteiro_relogio;
        Pagina* inicio = atual;
        
        do {
            if (atual->indice_pagina == dados.indice) {
                if(dados.tipo == 'R') {
                    printf("Referenciando página %d\n", dados.indice);
                    atual->R = 1;
                    atual->timestamp_ultima_ref = tempo_virtual_atual;
                    return;
                }
                else if(dados.tipo == 'M') {
                    printf("Modificando página %d\n", dados.indice);
                    atual->R = 1;
                    atual->M = 1;
                    atual->timestamp_ultima_ref = tempo_virtual_atual;
                    
                    // Atualiza também na cache se estiver lá
                    int cache_slot = buscar_na_cache(dados.indice);
                    if(cache_slot != -1) {
                        strncpy(cache_estruturada[cache_slot].mensagem, dados.mensagem, 2);
                        cache_estruturada[cache_slot].mensagem[2] = '\0';
                    }
                    
                    agendaEscrita(atual);
                    return;
                }
            }
            atual = atual->prox;
        } while (atual != inicio);
    }

    // TLB MISS - página não está na TLB
    printf("Page fault! Página %d não está na TLB\n", dados.indice);

    // Verifica na cache
    int cache_slot = buscar_na_cache(dados.indice);
    
    if(cache_slot != -1) {
        // AUSÊNCIA LEVE - página encontrada na cache
        printf("AUSÊNCIA LEVE! Página %d encontrada na cache\n", dados.indice);
        ausencia_leve++;
        listaProcessos[processo_atual].ausencia_leve++;
        
        // Cria operacao com dados da cache para carregar na TLB
        Operacao dados_cache;
        dados_cache.indice = dados.indice;
        dados_cache.tipo = dados.tipo;
        dados_cache.prot = cache_estruturada[cache_slot].prot;
        strcpy(dados_cache.mensagem, cache_estruturada[cache_slot].mensagem);
        
        // Carrega na TLB
        if(listaPaginas.qtd_paginas < limite_paginas) {
            adicionar_pagina_Relogio(dados_cache);
        } else {
            Pagina* nova = criar_pagina(dados_cache);
            if (nova) {
                substituirPagina(nova);
            }
        }
    } else {
        // AUSÊNCIA COMPLETA - página não está na cache, precisa buscar no disco
        printf("AUSÊNCIA COMPLETA! Página %d não encontrada na cache, buscando no disco...\n", dados.indice);
        ausencia_completa++;
        listaProcessos[processo_atual].ausencia_completa++;
        
        // Carrega página do disco para cache
        cache_slot = carregar_pagina_para_cache(dados.indice);
        
        if(cache_slot != -1) {
            // Cria operacao com dados da cache para carregar na TLB
            Operacao dados_disco;
            dados_disco.indice = dados.indice;
            dados_disco.tipo = dados.tipo;
            dados_disco.prot = cache_estruturada[cache_slot].prot;
            strcpy(dados_disco.mensagem, cache_estruturada[cache_slot].mensagem);
            
            // Se é uma operação de modificação, atualiza a mensagem
            if(dados.tipo == 'M') {
                strcpy(dados_disco.mensagem, dados.mensagem);
                strcpy(cache_estruturada[cache_slot].mensagem, dados.mensagem);
            }
            
            // Carrega na TLB
            if(listaPaginas.qtd_paginas < limite_paginas) {
                adicionar_pagina_Relogio(dados_disco);
            } else {
                Pagina* nova = criar_pagina(dados_disco);
                if (nova) {
                    substituirPagina(nova);
                }
            }
        } else {
            printf("ERRO: Não foi possível carregar página %d do disco\n", dados.indice);
        }
    }
}

// Função para imprimir estado da cache
void imprimir_estado_cache() {
    printf("\n=== Estado da Cache ===\n");
    printf("Slots ocupados: %d/%d\n", cache_ocupada, TAMANHO_MEMORIA_CACHE);
    
    int ocupados_reais = 0;
    for(int i = 0; i < TAMANHO_MEMORIA_CACHE; i++) {
        if(cache_estruturada[i].valida) {
            printf("Cache[%d]: virtual=%d, disco=%d, proc=%d, timestamp=%d, msg=%s\n",
                   i, cache_estruturada[i].indice_virtual, 
                   cache_estruturada[i].indice_real_disco,
                   cache_estruturada[i].processo_dono,
                   cache_estruturada[i].timestamp_uso,
                   cache_estruturada[i].mensagem);
            ocupados_reais++;
        }
    }
    cache_ocupada = ocupados_reais; // Correção da contagem
    printf("======================\n\n");
}
// Imprime estado atual do sistema
void imprimir_estado_sistema() {
    
    printf("\n=== Estado da TLB ===\n");
    printf("Tempo virtual atual: %d\n", tempo_virtual_atual);
    printf("Quantidade de páginas: %d\n", listaPaginas.qtd_paginas);
    printf("Page faults até agora: %d\n", ausencia_leve + ausencia_completa);
    
    if (listaPaginas.qtd_paginas > 0) {
        printf("Páginas na TLB:\n");
        Pagina* atual = listaPaginas.ponteiro_relogio;
        Pagina* inicio = atual;
        
        do {
            printf("  Página %d: R=%d, M=%d, timestamp=%d, working_set=%s%s\n",
                   atual->indice_pagina, atual->R, atual->M, atual->timestamp_ultima_ref,
                   esta_no_working_set(atual) ? "SIM" : "NAO",
                   (atual == listaPaginas.ponteiro_relogio) ? " [PONTEIRO]" : "");
            atual = atual->prox;
        } while (atual != inicio && listaProcessos[processo_atual].total_operacoes);

        int i = filaAgendamento.pagina_atual;
        printf(" Fila de agendamento de escrita:\nTempo virtual atual=%d\nTamanho da fila=%d\n", filaAgendamento.tempo_virtual, filaAgendamento.qtd_paginas);

        while(i < filaAgendamento.qtd_paginas){
            atual = &filaAgendamento.lista[i];
            printf("Página %d: R=%d, M=%d, timestamp=%d\n",
                atual->indice_pagina, atual->R, atual->M, atual->timestamp_ultima_ref);
            i++;
        }
    }
    
    printf("========================\n\n");
    
}

// Simulação de referências para cada processo
void simular_referencias(int processo) {
    printf("Iniciando simulação de referências...\n");
    
    imprimir_estado_sistema();

    Operacao atual;
    
    // Simula alternãncia entre processos
    // Só encerra no momento que todos processos finalizaram todas suas operações
    while(operacoes_global > 0){

        printf("\n==== Processo Atual executando:%d ====\n", processo);
        printf("==== Operações do processo restantes:%d ====\n", listaProcessos[processo].operacoes_rest);
        printf("==== Operações Globais restantes:%d ====\n", operacoes_global);

        if(listaProcessos[processo].operacoes_rest == 0){
            processo = (processo + 1) % max_processos;  // só troca de processo
            continue;
        }

        int indice_atual = (listaProcessos[processo].ultimo_indice == -1) ? 0 : listaProcessos[processo].ultimo_indice;
        int rest = listaProcessos[processo].operacoes_rest;
        int uso_atual = (rest > limite_uso_cpu) ? limite_uso_cpu : rest;
        
        for (int i = 0; i < uso_atual; i++) {
            atual = listaProcessos[processo].sequencia_operacoes[indice_atual];

            printf("\n--- Realiza operação %d: página %d ---\n", i+1, atual.indice);
            tempo_virtual_atual++;
            referenciar_pagina(atual);
            imprimir_estado_sistema();
            imprimir_estado_cache();
            indice_atual++;
        }
        listaProcessos[processo].ultimo_indice = indice_atual;
        listaProcessos[processo].operacoes_rest -= uso_atual;
        operacoes_global -= uso_atual;

        processo = (processo + 1) % max_processos;

        processo_atual = processo;
    }
    
}

// Inicialização do sistema
void inicializar_wsclock() {
    listaPaginas.qtd_paginas = 0;
    listaPaginas.tempo_virtual = 0;
    listaPaginas.ponteiro_relogio = NULL;
    
    printf("Sistema WSClock inicializado\n");
    printf("Janela working set: %d\n", JANELA_WORKING_SET);
    printf("Limite de páginas: %d\n", limite_paginas);

    inicializar_cache();
    imprimir_estado_cache();

    // Sistema de leitura de datasets sintéticos
    leituraArquivo();

    for(int disco_index = 0; disco_index < total_paginas+max_processos; disco_index++){
        printf("Disco[%d]:indice_real_disco->%d, prot->%d, mensagem->%s\n", 
               disco_index, disco[disco_index].indice, 
               disco[disco_index].prot, disco[disco_index].mensagem);
    }

}

void leituraArquivo(){

    FILE *entrada = fopen("Exemplo.txt", "r");

    // Inicializa o disco, memória e fila de agendamento
    disco = (Operacao*) malloc(TAMANHO_MEMORIA_DISCO * sizeof(Operacao));
    filaAgendamento.lista = (Pagina*) malloc((TAMANHO_MEMORIA_CACHE/8) * sizeof(Pagina));
    filaAgendamento.qtd_paginas = 0;
    filaAgendamento.pagina_atual = 0;
    filaAgendamento.tempo_virtual = 0;

    fscanf(entrada, "%d %d %d\n\n", &max_processos, &limite_paginas, &limite_uso_cpu);
    
    listaProcessos = (Processo*) malloc(max_processos * sizeof(Processo));

    int j, cont = 0;

    for(int i = 0; i < max_processos; i++){

        fscanf(entrada, "%d %d\n%d\n", &listaProcessos[i].indice_processo, &listaProcessos[i].total_operacoes, &listaProcessos[i].qtd_paginas);
        
        listaProcessos[i].operacoes_rest = listaProcessos[i].total_operacoes;
        listaProcessos[i].ultimo_indice = -1;
        operacoes_global += listaProcessos[i].total_operacoes;

        total_paginas += listaProcessos[i].qtd_paginas;
        int disco_index = i + cont;
        for(j = 0; j < listaProcessos[i].qtd_paginas; j++, cont++){
            disco_index = i + cont;

            fscanf(entrada, "%d %d %s\n", &disco[disco_index].indice, &disco[disco_index].prot, disco[disco_index].mensagem);
            
        }
        // Definição do limite de memória de cada processo
        disco[disco_index+1].indice = -1;
        disco[disco_index+1].prot = i;

        listaProcessos[i].sequencia_operacoes = (Operacao*) malloc(listaProcessos[i].total_operacoes * sizeof(Operacao));

        for(int k = 0; k < listaProcessos[i].total_operacoes; k++){
            char* tipo_operacao = (char*) malloc(2 * sizeof(char));
            fscanf(entrada, "\n%s %d", tipo_operacao, &listaProcessos[i].sequencia_operacoes[k].indice);
            
            if(tipo_operacao[0] == 'M'){
                fscanf(entrada, " %s\n", listaProcessos[i].sequencia_operacoes[k].mensagem);
            }

            listaProcessos[i].sequencia_operacoes[k].tipo = tipo_operacao[0];
            
        }

        fscanf(entrada, "\n");

    }

    operacoes_totais = operacoes_global;

    fclose(entrada);
}

// Função para inicializar a cache estruturada
void inicializar_cache() {
    for(int i = 0; i < TAMANHO_MEMORIA_CACHE; i++) {
        cache_estruturada[i].indice_virtual = -1;
        cache_estruturada[i].indice_real_disco = -1;
        cache_estruturada[i].processo_dono = -1;
        cache_estruturada[i].valida = 0;
        cache_estruturada[i].timestamp_uso = 0;
        cache_estruturada[i].mensagem[0] = '\0';
        cache_estruturada[i].mensagem[1] = '\0';
        cache_estruturada[i].mensagem[2] = '\0';
        cache_estruturada[i].prot = -1;
    }
    cache_ocupada = 0;
    cache_lru_counter = 0;
    printf("Cache inicializada com %d slots\n", TAMANHO_MEMORIA_CACHE);
}

// Função para buscar uma página na cache
int buscar_na_cache(int indice_virtual) {
    for(int i = 0; i < TAMANHO_MEMORIA_CACHE; i++) {
        if(cache_estruturada[i].valida && 
           cache_estruturada[i].indice_virtual == indice_virtual) {
            // Atualiza timestamp LRU
            cache_estruturada[i].timestamp_uso = cache_lru_counter++;
            printf("Cache HIT! Página %d encontrada na cache slot %d\n", indice_virtual, i);
            return i; // Retorna o índice da cache onde está a página
        }
    }
    printf("Cache MISS! Página %d não encontrada na cache\n", indice_virtual);
    return -1; // Não encontrado na cache
}

// Função para encontrar um slot livre na cache ou aplicar LRU
int encontrar_slot_cache() {
    // Primeiro, procura por um slot vazio
    for(int i = 0; i < TAMANHO_MEMORIA_CACHE; i++) {
        if(!cache_estruturada[i].valida) {
            printf("Slot livre encontrado na cache: %d\n", i);
            return i;
        }
    }
    
    // Se não há slots vazios, aplica LRU (Least Recently Used)
    int lru_slot = 0;
    int menor_timestamp = cache_estruturada[0].timestamp_uso;
    
    for(int i = 1; i < TAMANHO_MEMORIA_CACHE; i++) {
        if(cache_estruturada[i].timestamp_uso < menor_timestamp) {
            menor_timestamp = cache_estruturada[i].timestamp_uso;
            lru_slot = i;
        }
    }
    
    printf("Cache cheia! Aplicando LRU, removendo página %d do slot %d\n", 
           cache_estruturada[lru_slot].indice_virtual, lru_slot);
    
    return lru_slot;
}

// Função para carregar uma página do disco para a cache
int carregar_pagina_para_cache(int indice_virtual) {
    // Encontra a página no disco
    int disco_index = -1;
    int offset_atual = 0;
    
    // Procura em qual processo está a página e seu offset no disco
    for(int p = 0; p <= max_processos; p++) {
        if(p == max_processos) {
            printf("ERRO: Página %d não encontrada no disco!\n", indice_virtual);
            return -1;
        }
        
        // Calcula o range de índices virtuais para este processo
        int inicio_virtual = 100 + offset_atual;
        int fim_virtual = inicio_virtual + listaProcessos[p].qtd_paginas - 1;
        
        if(indice_virtual >= inicio_virtual && indice_virtual <= fim_virtual) {
            // Página pertence ao processo p
            disco_index = offset_atual + (indice_virtual - inicio_virtual);
            break;
        }
        
        offset_atual += listaProcessos[p].qtd_paginas + 1; // +1 para o marcador
    }
    
    if(disco_index == -1 || disco_index >= TAMANHO_MEMORIA_DISCO) {
        printf("ERRO: Índice de disco inválido para página %d\n", indice_virtual);
        return -1;
    }
    
    // Verifica se a página existe no disco
    if(disco[disco_index].indice == -1) {
        printf("ERRO: Página %d não existe no disco (índice %d)\n", indice_virtual, disco_index);
        return -1;
    }
    
    // Encontra um slot na cache
    int cache_slot = encontrar_slot_cache();
    
    // Carrega a página do disco para a cache
    cache_estruturada[cache_slot].indice_virtual = indice_virtual;
    cache_estruturada[cache_slot].indice_real_disco = disco[disco_index].indice;
    cache_estruturada[cache_slot].processo_dono = processo_atual;
    cache_estruturada[cache_slot].valida = 1;
    cache_estruturada[cache_slot].timestamp_uso = cache_lru_counter++;
    cache_estruturada[cache_slot].prot = disco[disco_index].prot;
    
    // Copia a mensagem
    cache_estruturada[cache_slot].mensagem[0] = disco[disco_index].mensagem[0];
    cache_estruturada[cache_slot].mensagem[1] = disco[disco_index].mensagem[1];
    cache_estruturada[cache_slot].mensagem[2] = '\0';
    
    if(!cache_estruturada[cache_slot].valida) {
        cache_ocupada++;
    }
    
    printf("Página %d carregada do disco (índice %d) para cache slot %d\n", 
           indice_virtual, disco_index, cache_slot);
    
    return cache_slot;
}



void escritaArquivo(){

    FILE *saida = fopen("Saida_wsclock.txt", "w");

    fprintf(saida, "PROCS=%d -- PÁG_T=%d -- OPER_T=%d -- AL=%d -- AC=%d\n", max_processos, total_paginas, operacoes_totais, ausencia_leve, ausencia_completa);

    for(int i = 0; i < max_processos; i++){
        fprintf(saida, "PROC=%d -- PÁG=%d -- AL=%d -- AC=%d\n", listaProcessos[i].indice_processo, listaProcessos[i].qtd_paginas, listaProcessos[i].ausencia_leve, listaProcessos[i].ausencia_completa);
    }

    fclose(saida);
}

int main() {
    printf("=== Implementação WSClock ===\n");

    pthread_mutex_init(&mutex_disco, NULL);
    pthread_mutex_init(&mutex_fila, NULL);
    
    // Exemplo de uso
    inicializar_wsclock();
    
    // Cria thread de timer (opcional, para demonstração)
    pthread_t escrita_em_disco;
    // pthread_create(&escrita_em_disco, NULL, escritaDisco, NULL);
    
    // Executa simulação
    simular_referencias(0);
    
    // Finaliza sistema
    sistema_ativo = 0;
    // pthread_join(escrita_em_disco, NULL);

    escritaArquivo();
    
    printf("\nSistema finalizado\n");

    return 0;
}