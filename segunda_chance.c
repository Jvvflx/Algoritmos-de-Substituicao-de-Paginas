#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#define TAMANHO_MEMORIA_CACHE 1024
#define TAMANHO_MEMORIA_DISCO 4096
#define JANELA_WORKING_SET 3

typedef struct pagina {
    int timestamp_ultima_ref;   // Timestamp da última referência (essencial para WSClock) G-rever para Segunda Chance
    int valida;                // Bit de validade
    int R;                     // Bit de referência
    int M;                     // Bit de modificação
    int prot;                  // Bit de proteção
    int indice_pagina;         // Índice da página virtual (simplificado)
    struct pagina *prox;       // Próxima página no relógio
} Pagina;

typedef struct
{
    Pagina *inicio, *fim;
    int quant;
} ListaSegundaChance;

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
    int qtd_page_fault;
    int total_operacoes;
    Operacao* sequencia_operacoes;
    int ultimo_indice;
}Processo;

//variaveis globais
ListaSegundaChance listaPaginas;
Processo* listaProcessos;
Fila filaAgendamento;
Operacao* disco;
Operacao* cache;
int total_paginas = 0;
int max_processos = 0;
int limite_paginas = 0;
int limite_uso_cpu = 0;
int tempo_virtual_atual = 0;
int processo_atual = 0;
int operacoes_global = 0;
int sistema_ativo = 1;

FILE *saida;

pthread_mutex_t mutex_disco;
pthread_mutex_t mutex_fila;

// Variáveis de comparação
int qtd_page_fault = 0;

// Protótipos das funções
Pagina* criar_pagina(Operacao dados);
void adicionar_pagina_SegundaChance(Operacao dados);
void leituraArquivo();
void mascararIndicesVirtuais();
void inicializarSegundaChance();
void escritaArquivo();
void imprimir_estado_sistema();
void travamentoDuasEtapas();
void destravamentoDuasEtapas();
void adicionar_pagina_Fila(Pagina *page);
void trocaPagina(Pagina *antiga, Pagina *nova);
void agendaEscrita(Pagina *page);
void substituirPagina(Pagina *nova_pagina);
void referenciar_pagina(Operacao dados);
void simular_referencias(int processo);

// Thread para simular o tempo virtual de forma controlada
void* timer_thread(void* args) {
    while(sistema_ativo) {
        tempo_virtual_atual++;
        sleep(1);
        
        // Tempo virtual só incrementa com atividade do sistema
        // Esta thread apenas simula a passagem de tempo
    }
    return NULL;
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
    
    return nova;
}

void adicionar_pagina_SegundaChance(Operacao dados) {
    
    if(listaPaginas.quant >= limite_paginas) {
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
    
    if (listaPaginas.quant == 0) {
        listaPaginas.fim = nova;
    } 
    else {

        Pagina* ultimo = listaPaginas.fim;

        while (ultimo->prox != listaPaginas.inicio)
        {
            ultimo = ultimo->prox;
        }
        
        nova->prox = listaPaginas.inicio;
        ultimo->prox = nova;
        listaPaginas.fim = nova;
    }
    
    listaPaginas.quant++;
    printf("Página %d adicionada ao sistema. Total: %d páginas no relógio\n", 
           dados.indice, listaPaginas.quant);
    
}

void leituraArquivo()
{
    FILE *entrada = fopen("Exemplo.txt", "r");
    saida = fopen("Saida.txt", "w");

    // Inicializa o disco, memória e fila de agendamento
    disco = (Operacao*) malloc(TAMANHO_MEMORIA_DISCO * sizeof(Operacao));
    cache = (Operacao*) malloc(TAMANHO_MEMORIA_CACHE * sizeof(Operacao));
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

        for(j = 0; j < listaProcessos[i].qtd_paginas; j++, cont++){

            fscanf(entrada, "%d %d %s\n", &disco[i+cont].indice, &disco[i+cont].prot, disco[i+cont].mensagem);
            
        }
        // Definição do limite de memória de cada processo
        disco[i+cont].indice = -1;
        disco[i+cont].prot = i;

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

    fprintf(saida, "%d %d %d %d\n", max_processos, total_paginas, operacoes_global, qtd_page_fault);


    fclose(entrada);
}

void mascararIndicesVirtuais()
{
    for(int i = 0; i < total_paginas+max_processos; i++)
    {
        cache[i].prot = disco[i].prot;
        cache[i].mensagem[0] = disco[i].mensagem[0];
        cache[i].mensagem[1] = disco[i].mensagem[1];

        cache[i].indice = disco[i].indice == -1 ? -1 : 100 + i;


        printf("cache: index->%d prot->%d mensagem->%s\n", cache[i].indice, cache[i].prot, cache[i].mensagem);   
    }
}

void inicializarSegundaChance()
{
    listaPaginas.fim = NULL;
    listaPaginas.quant = 0;
    listaPaginas.inicio = NULL;

    printf("Sistema Segunda Chance inicializado\n");
    printf("Limite de páginas: %d\n", limite_paginas);

    // Sistema de leitura de datasets sintéticos
    leituraArquivo();
    mascararIndicesVirtuais();
}

void escritaArquivo()
{
    for(int i = 0; i < max_processos; i++)
    {
        fprintf(saida, "%d %d %d\n", listaProcessos[i].indice_processo, listaProcessos[i].qtd_paginas, listaProcessos[i].qtd_page_fault);
    }

    fclose(saida);
}

void imprimir_estado_sistema()
{    
    printf("\n=== Estado do Sistema ===\n");
    printf("Tempo virtual atual: %d\n", tempo_virtual_atual);
    printf("Quantidade de páginas: %d\n", listaPaginas.quant);
    printf("Page faults até agora: %d\n", qtd_page_fault);
    
    if (listaPaginas.quant > 0)
    {
        printf("Páginas no sistema:\n");
        Pagina* atual = listaPaginas.fim;
        Pagina* inicio = atual;
        
        do {
            printf("  Página %d: R=%d, M=%d, timestamp=%d\n", atual->indice_pagina, atual->R, atual->M, atual->timestamp_ultima_ref);
            atual = atual->prox;
        } while (atual != inicio && listaProcessos[processo_atual].total_operacoes);

        int i = filaAgendamento.pagina_atual;
        printf(" Fila de agendamento de escrita:\nTempo virtual atual=%d\nTamanho da fila=%d\n", filaAgendamento.tempo_virtual, filaAgendamento.qtd_paginas);

        while(i < filaAgendamento.qtd_paginas)
        {
            atual = &filaAgendamento.lista[i];
            printf("Página %d: R=%d, M=%d, timestamp=%d\n",
                atual->indice_pagina, atual->R, atual->M, atual->timestamp_ultima_ref);
            i++;
        }
    }
    
    printf("========================\n\n");
    
}

void travamentoDuasEtapas()
{
    // Verifica se ambos estão livres para poder realizar a trava em ambos
    if(mutex_fila.__align)
    {
        pthread_mutex_trylock(&mutex_fila);
        if(mutex_disco.__align)
        {
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

void* escritaDisco(void *args)
{
    while(sistema_ativo){
        if(tempo_virtual_atual % limite_uso_cpu == 0 && tempo_virtual_atual > 0){
            travamentoDuasEtapas();

            if(filaAgendamento.qtd_paginas > 0)
            {
                for(int i = filaAgendamento.pagina_atual; i < filaAgendamento.qtd_paginas; filaAgendamento.pagina_atual++)
                {
                    int indice_pagina = filaAgendamento.lista[i].indice_pagina;
                    int indice_memoria = indice_pagina - 100;

                    disco[indice_memoria].mensagem[0] = cache[indice_memoria].mensagem[0];
                    disco[indice_memoria].mensagem[1] = cache[indice_memoria].mensagem[1];
                    
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

// Adiciona uma página ao sistema (estrutura circular)
void adicionar_pagina_Fila(Pagina *page) 
{    
    filaAgendamento.lista[filaAgendamento.qtd_paginas] = *page;
    filaAgendamento.qtd_paginas++;
    printf("Página %d adicionada ao sistema. Total: %d páginas na fila\n", 
           page->indice_pagina, filaAgendamento.qtd_paginas);
    
}

// Função para troca uma página antiga por uma nova
void trocaPagina(Pagina *antiga, Pagina *nova) 
{
    // CORREÇÃO: Verifica se há apenas uma página no sistema
    if(listaPaginas.quant == 1) 
    {
        listaPaginas.fim = nova;
        nova->prox = nova;
    } 
    else {
        // Copia dados importantes
        nova->prox = antiga->prox;

        // Atualiza ponteiros das páginas vizinhas
        Pagina* anterior = listaPaginas.inicio;
        while (anterior->prox != antiga)
        {
            anterior = anterior->prox;
        }
        
        // Atualiza ponteiro do relógio se necessário
        if (antiga == listaPaginas.inicio)
        {
            listaPaginas.inicio = nova;
        }
        
        if(listaPaginas.fim == antiga) {
            listaPaginas.fim = nova;
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

    // Adiciona página na fila de escrita
    for(int i = 0; i < filaAgendamento.qtd_paginas; i++){
        if(page->indice_pagina == filaAgendamento.lista[i].indice_pagina){
            printf("Página %d já está na fila para escrita em disco\n", page->indice_pagina);
            return;
        }
    }

    pthread_mutex_lock(&mutex_fila);
    adicionar_pagina_Fila(page);
    filaAgendamento.tempo_virtual = tempo_virtual_atual;
    pthread_mutex_unlock(&mutex_fila);

    printf("Página %d adicionada na fila para escrita no disco\n", page->indice_pagina);
    
}

// Implementação da segunda chance
void substituirPagina(Pagina *nova_pagina) {
    if (listaPaginas.quant == 0) {
        printf("Sistema vazio, adicionando primeira página\n");
        adicionar_pagina_SegundaChance((Operacao){nova_pagina->R ? 'R' : 'M', nova_pagina->indice_pagina, nova_pagina->prot, ""});
        return;
    }
    
    Pagina* atual = listaPaginas.fim;
    Pagina* inicio_busca = atual;
    
    printf("\n--- Iniciando substituição Segunda Chance ---\n");
    
    while(1) {
    
        // Bit R está setado
        if(atual->R == 1) {
            atual->R = 0;  // Limpa o bit R
            atual->timestamp_ultima_ref = tempo_virtual_atual;  // Atualiza timestamp
        }
        // Bit R não está setado
        else {
            // Página fora do working set, pode ser substituída
            if(atual->M == 1) {
                // Página suja, agenda para escrita
                agendaEscrita(atual);
                atual->M = 0;  // Limpa bit M após agendar escrita
                // Continua procurando por uma página limpa fora do working set
            } else {
                // Página limpa - substitui
                trocaPagina(atual, nova_pagina);
                break;
            }          
        }
        
        // Avança para próxima página
        atual = atual->prox;
    }
    
    // Atualiza ponteiro do relógio para próxima posição
    listaPaginas.fim = nova_pagina->prox;
    
    printf("--- Substituição Segunda Chance concluída ---\n\n");
}

// Função para simular referência a uma página
void referenciar_pagina(Operacao dados) {
    
    // Procura a página na lista
    if (listaPaginas.quant == 0) {
        printf("Sistema vazio! Carregando primeira página.\n");
        qtd_page_fault++;
        listaProcessos[processo_atual].qtd_page_fault++;
        adicionar_pagina_SegundaChance(dados);
        return;
    }
    
    Pagina* atual = listaPaginas.fim;
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
                
                int indice_memoria = atual->indice_pagina - 100;
                if(indice_memoria >= 0 && indice_memoria < TAMANHO_MEMORIA_CACHE) {
                    strncpy(cache[indice_memoria].mensagem, dados.mensagem, 2);
                    cache[indice_memoria].mensagem[2] = '\0';
                }
                
                agendaEscrita(atual);
                return;
            }
        }
        atual = atual->prox;
    } while (atual != inicio);

    qtd_page_fault++;
    listaProcessos[processo_atual].qtd_page_fault++;
    
    // Page fault - página não encontrada
    printf("Page fault! Página %d não está na memória\n", dados.indice);
    
    // Verifica se há espaço antes de adicionar
    if(listaPaginas.quant < limite_paginas) {
        adicionar_pagina_SegundaChance(dados);
    } else {
        Pagina* nova = criar_pagina(dados);
        if (nova) {
            substituirPagina(nova);
        }
    }
}

// Simulação de referências para cada processo
void simular_referencias(int processo) {
    printf("Iniciando simulação de referências...\n");
    
    // CORREÇÃO: Carrega páginas iniciais somente se há dados válidos
    for (int i = 0; i < limite_paginas && i < total_paginas && cache[i].indice != -1; i++) {
        adicionar_pagina_SegundaChance(cache[i]);
    }
    
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
            indice_atual++;
        }
        listaProcessos[processo].ultimo_indice = indice_atual;
        listaProcessos[processo].operacoes_rest -= uso_atual;
        operacoes_global -= uso_atual;

        processo = (processo + 1) % max_processos;

        processo_atual = processo;
    }
    
}

int main() {
    printf("=== Implementação Segunda Chance ===\n");

    pthread_mutex_init(&mutex_disco, NULL);
    pthread_mutex_init(&mutex_fila, NULL);
    
    // Exemplo de uso
    inicializarSegundaChance();
    
    // Cria thread de timer (opcional, para demonstração)
    pthread_t escrita_em_disco;
    pthread_create(&escrita_em_disco, NULL, escritaDisco, NULL);
    
    // Executa simulação
    simular_referencias(0);
    
    // Finaliza sistema
    sistema_ativo = 0;
    pthread_join(escrita_em_disco, NULL);

    escritaArquivo();
    
    printf("\nSistema finalizado\n");

    return 0;
}