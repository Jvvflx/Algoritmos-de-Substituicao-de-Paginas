#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define TAMANHO_MEMORIA_CACHE 1024
#define TAMANHO_MEMORIA_DISCO 4096
#define JANELA_WORKING_SET 10  // Tamanho da janela do working set

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

typedef struct operacao{
    int indice;
    int prot;
    char mensagem[3];
}Operacao;

typedef struct processo{
    int indice_processo;
    int qtd_paginas;
    int total_operacoes;
    Operacao* sequencia_operacoes;
    Operacao* ultima_operacao;
}Processo;

// Variáveis globais
Relogio listaPaginas;
Processo* listaProcessos;
Operacao* disco;
Operacao* cache;
int total_paginas;
int max_processos;
int limite_paginas;
int limite_uso_cpu;
int tempo_virtual_atual = 0;
int sistema_ativo = 1;

// Protótipos das funções
void substituirPagina(Pagina *nova_pagina);
void trocaPagina(Pagina *antiga, Pagina *nova);
void agendaEscrita(Pagina *page);
int esta_no_working_set(Pagina *page);
void adicionar_pagina(int indice);
void imprimir_estado_sistema();
void simular_referencias();
Pagina* criar_pagina(int indice);

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
Pagina* criar_pagina(int indice) {
    Pagina* nova = (Pagina*)malloc(sizeof(Pagina));
    if (!nova) return NULL;
    
    nova->indice_pagina = indice;
    nova->timestamp_ultima_ref = tempo_virtual_atual;
    nova->valida = 1;
    nova->R = 1; // Página recém carregada foi referenciada
    nova->M = 0; // Página limpa inicialmente
    nova->prot = 0;
    nova->prox = nova; // Inicialmente aponta para si mesmo
    nova->ante = nova;
    
    return nova;
}

// Adiciona uma página ao sistema (estrutura circular)
void adicionar_pagina(int indice) {
    
    Pagina* nova = criar_pagina(indice);
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
    printf("Página %d adicionada ao sistema. Total: %d páginas\n", 
           indice, listaPaginas.qtd_paginas);
    
}

// Implementação do WSClock
void substituirPagina(Pagina *nova_pagina) {
    if (listaPaginas.qtd_paginas == 0) {
        printf("Sistema vazio, adicionando primeira página\n");
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
    listaPaginas.ponteiro_relogio = atual->prox;
    
    
    printf("--- Substituição WSClock concluída ---\n\n");
}

// Função para verificar se a página está no working set
int esta_no_working_set(Pagina *page) {
    int diferenca_tempo = tempo_virtual_atual - page->timestamp_ultima_ref;
    return diferenca_tempo <= JANELA_WORKING_SET;
}

// Função para troca uma página antiga por uma nova
void trocaPagina(Pagina *antiga, Pagina *nova) {
    // Copia dados importantes
    nova->prox = antiga->prox;
    nova->ante = antiga->ante;

    // Atualiza ponteiros das páginas vizinhas
    antiga->ante->prox = nova;
    antiga->prox->ante = nova;
    
    // Configura nova página
    nova->R = 1;
    nova->timestamp_ultima_ref = tempo_virtual_atual;
    
    // Libera página antiga
    free(antiga);
}

// Função para agendar uma página suja para escrita no disco INCOMPLETA
void agendaEscrita(Pagina *page) {
    printf("Agendando escrita da página %d para disco\n", page->indice_pagina);

}

// Função para simular referência a uma página
void referenciar_pagina(int indice_pagina) {
    
    // Procura a página na lista
    if (listaPaginas.qtd_paginas == 0) {
        printf("Sistema vazio!\n");
        return;
    }
    
    Pagina* atual = listaPaginas.ponteiro_relogio;
    Pagina* inicio = atual;
    
    do {
        if (atual->indice_pagina == indice_pagina) {
            printf("Referenciando página %d\n", indice_pagina);
            atual->R = 1;
            atual->timestamp_ultima_ref = tempo_virtual_atual;
            return;
        }
        atual = atual->prox;
    } while (atual != inicio);
    
    // Page fault - página não encontrada
    printf("Page fault! Página %d não está na memória\n", indice_pagina);
    Pagina* nova = criar_pagina(indice_pagina); // Atualizar função para procurar paǵina do disco e criar página na tabela de paginação
    if (nova) {
        substituirPagina(nova);
    }
    
}

// Imprime estado atual do sistema
void imprimir_estado_sistema() {
    
    printf("\n=== Estado do Sistema ===\n");
    printf("Tempo virtual atual: %d\n", tempo_virtual_atual);
    printf("Quantidade de páginas: %d\n", listaPaginas.qtd_paginas);
    
    if (listaPaginas.qtd_paginas > 0) {
        printf("Páginas no sistema:\n");
        Pagina* atual = listaPaginas.ponteiro_relogio;
        Pagina* inicio = atual;
        
        do {
            printf("  Página %d: R=%d, M=%d, timestamp=%d, working_set=%s%s\n",
                   atual->indice_pagina, atual->R, atual->M, atual->timestamp_ultima_ref,
                   esta_no_working_set(atual) ? "SIM" : "NAO",
                   (atual == listaPaginas.ponteiro_relogio) ? " [PONTEIRO]" : "");
            atual = atual->prox;
        } while (atual != inicio);
    }
    
    printf("========================\n\n");
    
}

// Simulação de referências para teste
void simular_referencias() {
    printf("Iniciando simulação de referências...\n");
    
    // Adiciona algumas páginas iniciais
    for (int i = 1; i <= 20; i++) {
        adicionar_pagina(i);
    }
    
    imprimir_estado_sistema();
    
    // Simula algumas referências
    int sequencia[] = {1, 2, 3, 1, 4, 5, 6, 7, 8, 2, 1,1,1,2,3,1,6,12,14,14,15,17,19,1,1,2,3,2,3,12,14,14,14};
    int tamanho_seq = sizeof(sequencia) / sizeof(sequencia[0]);
    
    for (int i = 0; i < tamanho_seq; i++) {
        printf("\n--- Referência %d: página %d ---\n", i+1, sequencia[i]);
        referenciar_pagina(sequencia[i]);
        sleep(2);
        imprimir_estado_sistema();
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
    // Sistema de lietura de datasets sintéticos

}


void leituraArquivo(){

    FILE *entrada = fopen("Exemplo.txt", "r");
    FILE *saida = fopen("Saida.txt", "w");

    // Inicializa o disco e memória
    disco = (Operacao*) malloc(TAMANHO_MEMORIA_DISCO * sizeof(Operacao));
    cache = (Operacao*) malloc(TAMANHO_MEMORIA_CACHE * sizeof(Operacao));

    fscanf(entrada, "%d %d %d\n\n", &max_processos, &limite_paginas, &limite_uso_cpu);
    fprintf(saida, "%d %d %d\n\n", max_processos, limite_paginas, limite_uso_cpu);
    
    listaProcessos = (Processo*) malloc(max_processos * sizeof(Processo));

    int j, cont = 0;

    for(int i = 0; i < max_processos; i++){

        fscanf(entrada, "%d %d\n%d\n", &listaProcessos[i].indice_processo, &listaProcessos[i].total_operacoes, &listaProcessos[i].qtd_paginas);
        fprintf(saida, "%d %d\n%d\n", listaProcessos[i].indice_processo, listaProcessos[i].total_operacoes, listaProcessos[i].qtd_paginas);


        total_paginas += listaProcessos[i].qtd_paginas;

        for(j = 0; j < listaProcessos[i].qtd_paginas; j++, cont++){

            fscanf(entrada, "%d %d %s\n", &disco[i+cont].indice, &disco[i+cont].prot, disco[i+cont].mensagem);
            fprintf(saida, "%d %d %s\n", disco[i+cont].indice, disco[i+cont].prot, disco[i+cont].mensagem);
            
        }
        // Definição do limite de memória de cada processo
        disco[i+cont].indice = -1;
        disco[i+cont].prot = i;

        listaProcessos[i].sequencia_operacoes = (Operacao*) malloc(listaProcessos[i].total_operacoes * sizeof(Operacao));

        for(int k = 0; k < listaProcessos[i].total_operacoes; k++){
            char* tipo_operacao = (char*) malloc(2 * sizeof(char));
            fscanf(entrada, "\n%s %d", tipo_operacao, &listaProcessos[i].sequencia_operacoes[k].indice);
            fprintf(saida, "\n%s %d", tipo_operacao, listaProcessos[i].sequencia_operacoes[k].indice);

            if(tipo_operacao[0] == 'M'){
                fscanf(entrada, " %s\n", listaProcessos[i].sequencia_operacoes[k].mensagem);
                fprintf(saida, " %s", listaProcessos[i].sequencia_operacoes[k].mensagem);
            }
            
            if(k+1 == listaProcessos[i].total_operacoes){
                fprintf(saida, "\n");
            }
            
        }

        fscanf(entrada, "\n");
        fprintf(saida, "\n");


    }

    fclose(entrada);
    fclose(saida);
}

void mascararIndicesVirtuais(){

    for(int i = 0; i < total_paginas+max_processos; i++){

        cache[i].prot = disco[i].prot;
        cache[i].mensagem[0] = disco[i].mensagem[0];
        cache[i].mensagem[1] = disco[i].mensagem[1];

        cache[i].indice = disco[i].indice == -1 ? -1 : i;


        printf("cache: index->%d prot->%d mensagem->%s\n", cache[i].indice, cache[i].prot, cache[i].mensagem);
        
    }

}

int main() {
    // printf("=== Implementação WSClock ===\n");
    
    // // Exemplo de uso
    // inicializar_wsclock();
    
    // // Cria thread de timer (opcional, para demonstração)
    // pthread_t timer_tid;
    // pthread_create(&timer_tid, NULL, timer_thread, NULL);
    
    // // Executa simulação
    // simular_referencias();
    
    // // Finaliza sistema
    // sistema_ativo = 0;
    // pthread_join(timer_tid, NULL);
    
    // printf("\nSistema finalizado\n");

    leituraArquivo();
    mascararIndicesVirtuais();
    return 0;
}