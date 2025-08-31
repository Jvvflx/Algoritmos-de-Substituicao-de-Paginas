#include <stdio.h>
#include <stdlib.h>

#define NUM_QUADROS 4 // Simula o tamanho da memória física

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
    tipoNo *inicio, *fim;
    int quant;
} tipoListaCircular;

void criarLista(tipoListaCircular *lista, int num_nos)
{
    if (num_nos == 0) return;

    lista->fim = NULL;
    lista->quant = 0;

    for (int i = 0; i < num_nos; i++) {
        tipoNo *novoNo = (tipoNo*) malloc(sizeof(tipoNo));

        if (!novoNo) {
            perror("Falha ao alocar memória para o nó");
            exit(EXIT_FAILURE);
        }

        novoNo->dado.numeroPag = -1;
        novoNo->dado.bitReferencia = 0;
        novoNo->proxNo = NULL;

        if (!lista->inicio) {
            lista->inicio = novoNo;
        } else {
            lista->fim->proxNo = novoNo;
        }
        lista->fim = novoNo;
    }
    lista->fim->proxNo = lista->inicio;
}

// int insereListaVazia(tipoListaCircular *lista, int valor)
// {
//     tipoNo *novoNo = (tipoNo *)malloc(sizeof(tipoNo));
//     if (novoNo == NULL)
//         return 0;
//     novoNo->dado.numeroPag = valor;
//     novoNo->dado.bitReferencia = 0;
//     novoNo->proxNo = novoNo;
//     lista->fim = novoNo;
//     lista->quant++;
//     return 1;
// }

// int insereNoFim(tipoListaCircular *lista, int valor)
// {
//     tipoNo *novoNo;
//     if (lista->fim == NULL)
//     {
//         insereListaVazia(lista, valor);
//         return 1;
//     }
//     else
//     {
//         novoNo = (tipoNo *)malloc(sizeof(tipoNo));
//         if (novoNo == NULL)
//             return 0;
//         novoNo->dado.numeroPag = valor;
//         novoNo->dado.bitReferencia = 0;
//         novoNo->proxNo = lista->fim->proxNo;
//         lista->fim->proxNo = novoNo;
//         lista->fim = novoNo;
//         lista->quant++;
//         return 1;
//     }
// }

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

void imprimirLista (const tipoListaCircular *lista, const tipoNo *ptr) {
    if (!lista->inicio) return;

    tipoNo *atual = lista->inicio;
    
    printf("Estado da memória: [");
    do {
        if (atual->dado.numeroPag == -1){
            printf("Vazio, R = 0");
        } else {
            printf("P: %d, R = %d", atual->dado.numeroPag, atual->dado.bitReferencia);
        }

        if (atual == ptr) {
            printf(" <-- Ponteiro");
        }

        atual = atual->proxNo;
        if (atual != ptr) {
            printf(", ");
        }
    } while (atual != ptr);

    printf("]\n\n");
}


void liberarLista (tipoListaCircular *lista) {
    if (!lista->inicio) return;

    tipoNo *atual = lista->inicio;
    tipoNo *inicio = lista->inicio;

    do {
        tipoNo *removerNo = atual;
        atual = atual->proxNo;
        free(removerNo);
    } while (atual != inicio);
}

int main() {
    int seqAcessos[] = {1, 3, 5, 2, 4, 1, 4, 5, 3, 2};
    int tamSeqAcessos = sizeof(seqAcessos) / sizeof(int);

    int pageFaults = 0;
    int pagePresent = 0;

    tipoListaCircular lista;
    criarLista(&lista, NUM_QUADROS);

    printf("==== INICIANDO SIMULAÇÃO ====\n");
    printf("Total de Quadros na memória: %d\n", NUM_QUADROS);

    tipoNo *ptr = lista.inicio;

    for (int i = 0; i < tamSeqAcessos; i++) {
        int pagAtual = seqAcessos[i];
        printf("--> acessando página: %d\n", pagAtual);

        int pagEncontrada = 0;
        tipoNo *noAtual = lista.inicio;

        for (int j = 0; j < NUM_QUADROS; j++) {
            if (noAtual->dado.numeroPag == pagAtual) {
                printf("[PAGE FOUND] Página %d já está na memória.\n", pagAtual);
                noAtual->dado.bitReferencia = 1;
                pagEncontrada = 1;
                pagePresent++;
                break; 
            }
            noAtual = noAtual->proxNo;
        }

        if (!pagEncontrada) {
            pageFaults++;
            printf("[PAGE FAULT] Página %d não está na memória.\n", pagAtual);

            while (1) {
                if (ptr->dado.bitReferencia == 0) { 
                    if (ptr->dado.numeroPag == -1) {
                        printf("Encontrado quadro vazio -> Inserindo página %d\n", pagAtual);

                    } else {
                        printf("Substituindo página %d pela página %d.\n", ptr->dado.numeroPag, pagAtual);

                    }
                    ptr->dado.numeroPag = pagAtual;
                    ptr->dado.bitReferencia = 0;
                    ptr = ptr->proxNo;
                    break; 

                } else {
                    ptr->dado.bitReferencia = 0; 
                    ptr = ptr->proxNo;     
                }
            }
        }
        imprimirLista(&lista, ptr);
    }

    printf("\n==== Fim da Simulação ====\n");
    printf("Total de Page Faults: %d\n", pageFaults);
    printf("Total de Founds: %d\n", pagePresent);

    liberarLista(&lista);
    
    return 0;
}