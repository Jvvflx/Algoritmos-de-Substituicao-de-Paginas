#include <stdio.h>
#include <stdlib.h>

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

int main() {}