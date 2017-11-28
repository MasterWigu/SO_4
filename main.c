/*
// Projeto SO - exercicio 3
// Sistemas Operativos, DEI/IST/ULisboa 2017-18
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>


#include "matrix2d.h"
#include "util.h"

/*--------------------------------------------------------------------
| Type: thread_info
| Description: Estrutura com Informacao para Trabalhadoras
---------------------------------------------------------------------*/

typedef struct {
  int    id;
  int    N;
  int    iter;
  int    trab;
  int    tam_fatia;
  double maxD;
} thread_info;

/*--------------------------------------------------------------------
| Type: doubleBarrierWithMax
| Description: Barreira dupla com variavel de max-reduction
---------------------------------------------------------------------*/

typedef struct {
  int             total_nodes;
  int             pending[2];
  double          maxdelta[2];
  int             iteracoes_concluidas;
  pthread_mutex_t mutex;
  pthread_cond_t  wait[2];
  char            *fich;
} DualBarrierWithMax;

/*--------------------------------------------------------------------
| Global variables
---------------------------------------------------------------------*/

DoubleMatrix2D     *matrix_copies[2];
DualBarrierWithMax *dual_barrier;
double              maxD;
int                 periodoS;
volatile int        guardar, parar;
int                 ja_guardou, existeFich;


/*--------------------------------------------------------------------
| Function: save
| Description: Mete a flag "guardar" a 1 para guardar para o ficheiro
---------------------------------------------------------------------*/
void save() {
  signal(SIGALRM, SIG_IGN); //desactiva o sinal ate ser guardado
  guardar = 1;
}

/*--------------------------------------------------------------------
| Function: stop
| Description: Mete a flag "parar" a 1 para parar no fim da iteracao
---------------------------------------------------------------------*/
void stop() {
  signal(SIGINT, SIG_IGN); //desactiva o sinal para evitar corrupcao ao ler a flag
  parar = 1;
}


/*--------------------------------------------------------------------
| Function: dualBarrierInit
| Description: Inicializa uma barreira dupla
---------------------------------------------------------------------*/

DualBarrierWithMax *dualBarrierInit(int ntasks, char* nFich) {
  DualBarrierWithMax *b;
  b = (DualBarrierWithMax*) malloc (sizeof(DualBarrierWithMax));
  if (b == NULL) return NULL;

  b->total_nodes = ntasks;
  b->pending[0]  = ntasks;
  b->pending[1]  = ntasks;
  b->maxdelta[0] = 0;
  b->maxdelta[1] = 0;
  b->iteracoes_concluidas = 0;
  b->fich = nFich;

  if (pthread_mutex_init(&(b->mutex), NULL) != 0) {
    fprintf(stderr, "\nErro a inicializar mutex\n");
    exit(1);
  }
  if (pthread_cond_init(&(b->wait[0]), NULL) != 0) {
    fprintf(stderr, "\nErro a inicializar variável de condição\n");
    exit(1);
  }
  if (pthread_cond_init(&(b->wait[1]), NULL) != 0) {
    fprintf(stderr, "\nErro a inicializar variável de condição\n");
    exit(1);
  }
  return b;
}

/*--------------------------------------------------------------------
| Function: dualBarrierFree
| Description: Liberta os recursos de uma barreira dupla
---------------------------------------------------------------------*/

void dualBarrierFree(DualBarrierWithMax* b) {
  if (pthread_mutex_destroy(&(b->mutex)) != 0) {
    fprintf(stderr, "\nErro a destruir mutex\n");
    exit(1);
  }
  if (pthread_cond_destroy(&(b->wait[0])) != 0) {
    fprintf(stderr, "\nErro a destruir variável de condição\n");
    exit(1);
  }
  if (pthread_cond_destroy(&(b->wait[1])) != 0) {
    fprintf(stderr, "\nErro a destruir variável de condição\n");
    exit(1);
  }
  free(b);
}

/*--------------------------------------------------------------------
| Function: dualBarrierWait
| Description: Ao chamar esta funcao, a tarefa fica bloqueada ate que
|              o numero 'ntasks' de tarefas necessario tenham chamado
|              esta funcao, especificado ao ininializar a barreira em
|              dualBarrierInit(ntasks). Esta funcao tambem calcula o
|              delta maximo entre todas as threads e devolve o
|              resultado no valor de retorno
---------------------------------------------------------------------*/

double dualBarrierWait (DualBarrierWithMax* b, int iter, double localmax) {
  int current = iter % 2;
  int next = 1 - current;
  int state_filho;

  if (pthread_mutex_lock(&(b->mutex)) != 0) {
    fprintf(stderr, "\nErro a bloquear mutex\n");
    exit(1);
  }
  // decrementar contador de tarefas restantes
  b->pending[current]--;
  // actualizar valor maxDelta entre todas as threads
  if (b->maxdelta[current]<localmax)
    b->maxdelta[current]=localmax;
  // verificar se sou a ultima tarefa
  if (b->pending[current]==0) {
    // sim -- inicializar proxima barreira e libertar threads
    b->iteracoes_concluidas++;
    b->pending[next]  = b->total_nodes;
    b->maxdelta[next] = 0;

    if (pthread_mutex_unlock(&(b->mutex)) != 0) {
      fprintf(stderr, "\nErro a desbloquear mutex\n");
      exit(1);
    }

    int pid_filho = 0; //nao faz nada (nao grava)(apenas para inicializar a var)
    if (guardar) {
      guardar = 0; //reset a flag
      //criar salvaguarda
      pid_filho = 1; // 1 e valor padrao para a primeira gravacao (faz sempre fork e nao faz waitpid)
      if (ja_guardou)
        pid_filho = waitpid(-1, &state_filho, WNOHANG); //pid fica a 0 se o processo filho ainda nao retornou
        if (WIFEXITED(state_filho) != 1)
          die("Erro no processo filho");
        if (pid_filho == -1)
          die("Erro no waitpid");
    }

    if (pid_filho) { //so faz fork se o processo filho tiver retornado
      int pid = fork();
      printf("FEZ FORK\n");
      if (pid == -1) //caso erro
        die("Erro no fork");

      if (pid == 0) { //se e tarefa filho
        printf("SOU FILHO\n");
        char *newFich = (char*) malloc((strlen(b->fich)+1)*sizeof(char));
        FILE *fp;
        
        newFich = strcpy(newFich, b->fich);
        newFich = strcat(newFich, "~");
        fp = fopen(newFich, "w");

        if (fp == NULL) 
          die("Erro ao abrir ficheiro");

        dm2dPrint(matrix_copies[current], fp);
        if (fclose(fp) !=0)
          die("Erro ao fechar ficheiro");
    
        if (existeFich && unlink(b->fich) != 0) //na primeira iteracao nao ha ficheiro para eliminar
          die("Erro ao apagar ficheiro para renomear");

        if (rename(newFich, b->fich) != 0)
          die("Erro ao renomear ficheiro");
        printf("SAI FILHO\n\n");
        exit(0);
      }
      else { //tem de ser feito pelo processo pai, se fosse feito pelo filho perdia-se a informacao ao terminar
        ja_guardou = 1; //ja salvou pelo menos uma vez
        existeFich = 1; //ja existe ficheiro
        alarm(periodoS); //mete um novo alarme
        signal(SIGALRM, save);  //volta a activar o sinal para guardar
      }
    }

    if (pthread_mutex_lock(&(b->mutex)) != 0) {
      fprintf(stderr, "\nErro a bloquear mutex\n");
      exit(1);
    }

    if (pthread_cond_broadcast(&(b->wait[current])) != 0) {
      fprintf(stderr, "\nErro a assinalar todos em variável de condição\n");
      exit(1);
    }
  }
  else {
    // nao -- esperar pelas outras tarefas
    while (b->pending[current]>0) {
      if (pthread_cond_wait(&(b->wait[current]), &(b->mutex)) != 0) {
        fprintf(stderr, "\nErro a esperar em variável de condição\n");
        exit(1);
      }
    }
  }
  double maxdelta = b->maxdelta[current];
  if (pthread_mutex_unlock(&(b->mutex)) != 0) {
    fprintf(stderr, "\nErro a desbloquear mutex\n");
    exit(1);
  }
  return maxdelta;
}

/*--------------------------------------------------------------------
| Function: tarefa_trabalhadora
| Description: Funcao executada por cada tarefa trabalhadora.
|              Recebe como argumento uma estrutura do tipo thread_info
---------------------------------------------------------------------*/

void *tarefa_trabalhadora(void *args) {
  thread_info *tinfo = (thread_info *) args;
  int tam_fatia = tinfo->tam_fatia;
  int my_base = tinfo->id * tam_fatia;
  double global_delta = INFINITY;
  int iter = 0;

  do {
    int atual = iter % 2;
    int prox = 1 - iter % 2;
    double max_delta = 0;

    // Calcular Pontos Internos
    for (int i = my_base; i < my_base + tinfo->tam_fatia; i++) {
      for (int j = 0; j < tinfo->N; j++) {
        double val = (dm2dGetEntry(matrix_copies[atual], i,   j+1) +
                      dm2dGetEntry(matrix_copies[atual], i+2, j+1) +
                      dm2dGetEntry(matrix_copies[atual], i+1, j) +
                      dm2dGetEntry(matrix_copies[atual], i+1, j+2))/4;
        // calcular delta
        double delta = fabs(val - dm2dGetEntry(matrix_copies[atual], i+1, j+1));
        if (delta > max_delta) {
          max_delta = delta;
        }
        dm2dSetEntry(matrix_copies[prox], i+1, j+1, val);
      }
    }
    // barreira de sincronizacao; calcular delta global
    global_delta = dualBarrierWait(dual_barrier, iter, max_delta);
  } while (parar != 1 && ++iter < tinfo->iter && global_delta >= tinfo->maxD);

  return 0;
}

/*--------------------------------------------------------------------
| Function: main
| Description: Entrada do programa
---------------------------------------------------------------------*/

int main (int argc, char** argv) {
  int N;
  double tEsq, tSup, tDir, tInf;
  int iter, trab;
  int tam_fatia;
  int res;
  char* fichS;
  FILE *f;
  int estado;

  if (argc != 11) {
    fprintf(stderr, "Utilizacao: ./heatSim N tEsq tSup tDir tInf iter trab maxD fichS periodoS\n\n");
    die("Numero de argumentos invalido");
  }

  // Ler Input
  N    = parse_integer_or_exit(argv[1], "N",    1);
  tEsq = parse_double_or_exit (argv[2], "tEsq", 0);
  tSup = parse_double_or_exit (argv[3], "tSup", 0);
  tDir = parse_double_or_exit (argv[4], "tDir", 0);
  tInf = parse_double_or_exit (argv[5], "tInf", 0);
  iter = parse_integer_or_exit(argv[6], "iter", 1);
  trab = parse_integer_or_exit(argv[7], "trab", 1);
  maxD = parse_double_or_exit (argv[8], "maxD", 0);
  fichS = argv[9];
  periodoS = parse_integer_or_exit(argv[10], "periodoS", 0);

  fprintf(stderr, "\nArgumentos:\n"
   " N=%d tEsq=%.1f tSup=%.1f tDir=%.1f tInf=%.1f iter=%d trab=%d maxD=%.4f fichS=%s periodoS=%d\n\n",
   N, tEsq, tSup, tDir, tInf, iter, trab, maxD, fichS, periodoS);

  if (N % trab != 0) {
    fprintf(stderr, "\nErro: Argumento %s e %s invalidos.\n"
                    "%s deve ser multiplo de %s.", "N", "trab", "N", "trab");
    return -1;
  }

  signal(SIGINT, stop);  
  signal(SIGALRM, save);
  ja_guardou = 0;        //flag que informa que ainda nunca foi feito nenhum fork (evita erro no waitpid)
  alarm(periodoS);       //inicia o primeiro alarme

  // Inicializar Barreira
  dual_barrier = dualBarrierInit(trab, fichS);
  if (dual_barrier == NULL)
    die("Nao foi possivel inicializar barreira");

  // Calcular tamanho de cada fatia
  tam_fatia = N / trab;

  // Criar e Inicializar Matrizes
  f = fopen(fichS, "r");
  if (f != NULL) {  //se existir uma salvaguarda
    matrix_copies[0] = readMatrix2dFromFile(f, N+2, N+2);
    matrix_copies[1] = dm2dNew(N+2, N+2);
    dm2dCopy(matrix_copies[1], matrix_copies[0]);

    if (matrix_copies[0] == NULL || matrix_copies[1] == NULL) {
      die("Erro ao criar matrizes");
    }
    fclose(f);
    existeFich = 1; 
  }
  else {
    matrix_copies[0] = dm2dNew(N+2,N+2);
    matrix_copies[1] = dm2dNew(N+2,N+2);

    if (matrix_copies[0] == NULL || matrix_copies[1] == NULL) {
      die("Erro ao criar matrizes");
    }

    dm2dSetLineTo (matrix_copies[0], 0, tSup);
    dm2dSetLineTo (matrix_copies[0], N+1, tInf);
    dm2dSetColumnTo (matrix_copies[0], 0, tEsq);
    dm2dSetColumnTo (matrix_copies[0], N+1, tDir);
    dm2dCopy (matrix_copies[1],matrix_copies[0]);
  }
  // Reservar memoria para trabalhadoras
  thread_info *tinfo = (thread_info*) malloc(trab * sizeof(thread_info));
  pthread_t *trabalhadoras = (pthread_t*) malloc(trab * sizeof(pthread_t));

  if (tinfo == NULL || trabalhadoras == NULL) {
    die("Erro ao alocar memoria para trabalhadoras");
  }

  // Criar trabalhadoras
  for (int i=0; i < trab; i++) {
    tinfo[i].id = i;
    tinfo[i].N = N;
    tinfo[i].iter = iter;
    tinfo[i].trab = trab;
    tinfo[i].tam_fatia = tam_fatia;
    tinfo[i].maxD = maxD;
    res = pthread_create(&trabalhadoras[i], NULL, tarefa_trabalhadora, &tinfo[i]);
    if (res != 0) {
      die("Erro ao criar uma tarefa trabalhadora");
    }
  }

  // Esperar que as trabalhadoras terminem
  for (int i=0; i<trab; i++) {
    res = pthread_join(trabalhadoras[i], NULL);
    if (res != 0)
      die("Erro ao esperar por uma tarefa trabalhadora");
  }

  //dm2dPrint (matrix_copies[dual_barrier->iteracoes_concluidas%2], stdout);

  printf("DONE\n");


  // Esperar pelo ultimo processo filho que foi criado (para poder apagar ficheiro)
    wait(&estado);

  // Apagar ficheiro (so se guardou alguma vez e se o programa nao esta a terminar por SIGINT)
  if (existeFich && parar != 1)
    if (unlink(fichS) != 0) {
      die("Erro ao apagar ficheiro");
   }

  // Libertar memoria
  dm2dFree(matrix_copies[0]);
  dm2dFree(matrix_copies[1]);
  free(tinfo);
  free(trabalhadoras);
  dualBarrierFree(dual_barrier);
  
  return 0;
}
 
