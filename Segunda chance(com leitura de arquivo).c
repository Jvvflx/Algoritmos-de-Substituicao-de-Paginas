#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define TAMANHO_MEMORIA_CACHE 1024
#define TAMANHO_MEMORIA_DISCO 4096
#define JANELA_WORKING_SET 10

// structs para leitura de Processos 


typedef struct operacao{
    char tipo;
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

// Variáveis Para leitura de Processos
Processo* listaProcessos;
Operacao* disco;
Operacao* cache;
int total_paginas;
int max_processos;
int limite_paginas;
int limite_uso_cpu;
int tempo_virtual_atual = 0;
int processo_atual = 0;
int uso_cpu = 0;
int sistema_ativo = 1;

typedef struct
{
    int numeroPag;
    int bitReferencia;
} Quadro;

typedef struct no
{
    Quadro dado;
    struct no *proxNo;
} tipoNo;

typedef struct
{
    tipoNo *fim;
    int quant;
} tipoListaCircular;

void inicializarLista(tipoListaCircular *lista)
{
    lista->fim = NULL;
    lista->quant = 0;
}

int insereListaVazia(tipoListaCircular *lista, int valor)
{
    tipoNo *novoNo = (tipoNo *)malloc(sizeof(tipoNo));
    if (novoNo == NULL)
        return 0;
    novoNo->dado.numeroPag = valor;
    novoNo->dado.bitReferencia = 0;
    novoNo->proxNo = novoNo;
    lista->fim = novoNo;
    lista->quant++;
    return 1;
}

int insereNoFim(tipoListaCircular *lista, int valor)
{
    tipoNo *novoNo;
    if (lista->fim == NULL)
    {
        insereListaVazia(lista, valor);
        return 1;
    }
    else
    {
        novoNo = (tipoNo *)malloc(sizeof(tipoNo));
        if (novoNo == NULL)
            return 0;
        novoNo->dado.numeroPag = valor;
        novoNo->dado.bitReferencia = 0;
        novoNo->proxNo = lista->fim->proxNo;
        lista->fim->proxNo = novoNo;
        lista->fim = novoNo;
        lista->quant++;
        return 1;
    }
}

// CRIEI PARA MUDAR O BIT DE REFERENCIA, VISTO QUE O CORRETO É INICIALIZAR COM 0
// AO CARREGAR A PAGINA DEVE CARREGAR COM 0 CARREGAR != USAR
int usarPagina(tipoListaCircular *lista, int numeroPag)
{
    if (lista == NULL || lista->fim == NULL)
        return 0;

    tipoNo *aux = lista->fim->proxNo;
    for (int i = 0; i < lista->quant; i++)
    {
        if (aux->dado.numeroPag == numeroPag)
        {
            aux->dado.bitReferencia = 1;
            return 1;
        }
        aux = aux->proxNo;
    }
    return 0;
}

int substituirPag(tipoListaCircular *lista, int novaPag)
{
    if (lista == NULL || lista->fim == NULL)
        return 0;

    while (1)
    {
        // aux = nó que deve ser removido, sempre começando a checagem no primeiro da fila (mais antigo)
        tipoNo *aux = lista->fim->proxNo;

        if (aux->dado.bitReferencia == 0)
        {
            aux->dado.numeroPag = novaPag;
            lista->fim = aux;
            return 1;
        }
        else
        {
            aux->dado.bitReferencia = 0;
            lista->fim = aux;
        }
    }
}


//leitura do Arquivo

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

            listaProcessos[i].sequencia_operacoes[k].tipo = tipo_operacao[0];
            
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

//main

int main() {}
