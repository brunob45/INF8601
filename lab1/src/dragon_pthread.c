/*
 * dragon_pthread.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "utils.h"
#include "dragon.h"
#include "color.h"
#include "dragon_pthread.h"

#define PRINT_PTHREAD_ERROR(err, msg) \
	do { errno = err; perror(msg); } while(0)

pthread_mutex_t mutex_stdout;

void printf_threadsafe(char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	pthread_mutex_lock(&mutex_stdout);
	vprintf(format, ap);
	pthread_mutex_unlock(&mutex_stdout);
	va_end(ap);
}

/**
 * Does the work on a part of the dragon.
 * Returns 0 on success.
 */
void* dragon_draw_worker(void *data)
{
	struct draw_data info = *((struct draw_data*)data);
	int area = info.dragon_width * info.dragon_height;

	/* 1. Initialiser la surface */
	int canvasStart = ((uint64_t)info.id)*area/(long int)info.nb_thread;
	int canvasEnd = ((uint64_t)(info.id+1))*area/(long int)info.nb_thread;
	
	init_canvas(canvasStart, canvasEnd, info.dragon, -1);

	pthread_barrier_wait(info.barrier);
	/* 2. Dessiner les dragons dans les 4 directions
	 *
	 * Il est attendu que chaque threads dessine une partie
	 * de chaque dragon.
	 * */

	uint64_t start = info.id * info.size / info.nb_thread;
	uint64_t end = (info.id + 1) * info.size / info.nb_thread;

	/*
		* Le premier argument (tile) contrôle la direction vers laquelle
		* le dragon est dessiné.
		*/
	for(int tile = 0; tile < NB_TILES; tile++) {
	if(dragon_draw_raw(tile, start, end, info.dragon, info.dragon_width, info.dragon_height, info.limits, info.id) < 0) 
		printf("begin: %ld, end: %ld\n", start, end);
	}

	pthread_barrier_wait(info.barrier);

	start = info.id * info.image_height / info.nb_thread;
	end = (info.id + 1) * info.image_height / info.nb_thread;

	/* 3. Effectuer le rendu final */
	scale_dragon(start, end, info.image, info.image_width, info.image_height, info.dragon, info.dragon_width, info.dragon_height, info.palette);
	pthread_barrier_wait(info.barrier);

	return NULL;
}

int dragon_draw_pthread(char **canvas, struct rgb *image, int width, int height, uint64_t size, int nb_thread)
{
	//TODO("dragon_draw_pthread");
	
	pthread_t *threads = NULL;
	pthread_barrier_t barrier;
	limits_t lim;
	struct draw_data info;
	char *dragon = NULL;
	int scale_x;
	int scale_y;
	struct draw_data *data = NULL;
	struct palette *palette = NULL;
	int ret = 0;

	palette = init_palette(nb_thread);
	if (palette == NULL)
		goto err;

	/* 1. Initialiser barrier. */
	if(pthread_barrier_init(&barrier, NULL, nb_thread) != 0) {
		printf("barrier init error\n");
		goto err;
	}

	info.dragon_width = lim.maximums.x - lim.minimums.x;
	info.dragon_height = lim.maximums.y - lim.minimums.y;

	if (dragon_limits_pthread(&lim, size, nb_thread) < 0)
		goto err;

	info.dragon_width = lim.maximums.x - lim.minimums.x;
	info.dragon_height = lim.maximums.y - lim.minimums.y;

	if ((dragon = (char *) malloc(info.dragon_width * info.dragon_height)) == NULL) {
		printf("malloc error dragon. width : %d, height : %d\n", info.dragon_width, info.dragon_height);
		goto err;
	}

	if ((data = malloc(sizeof(struct draw_data) * nb_thread)) == NULL) {
		printf("malloc error data\n");
		goto err;
	}

	if ((threads = malloc(sizeof(pthread_t) * nb_thread)) == NULL) {
		printf("malloc error threads\n");
		goto err;
	}

	info.image_height = height;
	info.image_width = width;
	scale_x = info.dragon_width / width + 1;
	scale_y = info.dragon_height / height + 1;
	info.scale = (scale_x > scale_y ? scale_x : scale_y);
	info.deltaJ = (info.scale * width - info.dragon_width) / 2;
	info.deltaI = (info.scale * height - info.dragon_height) / 2;
	info.nb_thread = nb_thread;
	info.dragon = dragon;
	info.image = image;
	info.size = size;
	info.limits = lim;
	info.barrier = &barrier;
	info.palette = palette;

	/* 2. Lancement du calcul parallèle principal avec dragon_draw_worker */
	for(int i = 0; i < nb_thread; i++)
	{
		data[i] = info;
		data[i].id = i;
		pthread_create(&threads[i], NULL, dragon_draw_worker, &data[i]);
	}

	/* 3. Attendre la fin du traitement */
	for(int i = 0; i < nb_thread; i++)
	{
		pthread_join(threads[i], NULL);
	}

	/* 4. Destruction des variables. */

	if(pthread_barrier_destroy(&barrier) != 0) {
		printf("barrier destroy error\n");
		goto err;
	}

done:
	FREE(data);
	FREE(threads);
	free_palette(palette);
	*canvas = dragon;
	//*canvas = NULL; // TODO: retourner le dragon calculé
	return ret;

err:
	FREE(dragon);
	ret = -1;
	goto done;
}

void *dragon_limit_worker(void *data)
{
	int i;
	struct limit_data *lim = (struct limit_data *) data;
	int start = lim->start;
	int end = lim->end;

	for (i = 0; i < NB_TILES; i++) {
		piece_limit(start, end, &lim->pieces[i]);
	}

	return NULL;
}

/*
 * Calcule les limites en terme de largeur et de hauteur de
 * la forme du dragon. Requis pour allouer la matrice de dessin.
 */
int dragon_limits_pthread(limits_t *limits, uint64_t size, int nb_thread)
{
	//TODO("dragon_limits_pthread");

	int ret = 0;
	pthread_t *threads = NULL;
	struct limit_data *thread_data = NULL;
	piece_t masters[NB_TILES];

	for (int tile = 0; tile < NB_TILES; tile++) {
		/**
		 * La pièce master représente les limites d'un dragon complet.
		 * Notez bien que chaque dragon à une orientation différente.
		 */
		piece_init(&masters[tile]);
		masters[tile].orientation = tiles_orientation[tile];
	}

	/* 1. Allouer de l'espace pour threads et threads_data. */
	if ((thread_data = calloc(nb_thread, sizeof(struct limit_data))) == NULL) {
		printf("malloc error data\n");
		goto err;
	}

	if ((threads = calloc(nb_thread, sizeof(pthread_t))) == NULL) {
		printf("malloc error threads\n");
		goto err;
	}
	
	/* 2. Lancement du calcul en parallèle avec dragon_limit_worker. */
	for(int thread = 0; thread < nb_thread; thread++)
	{
		thread_data[thread].id = thread;
		thread_data[thread].start = thread*size/nb_thread;
		thread_data[thread].end = (thread+1)*size/nb_thread;
		for(int tile =0; tile<NB_TILES;tile++){
			piece_init(&(thread_data[thread].pieces[tile]));
			thread_data[thread].pieces[tile].orientation = tiles_orientation[tile];
		}
		if(pthread_create(&threads[thread], 
		               NULL, 
					   dragon_limit_worker,
					   &thread_data[thread]) != 0) {
						   printf("pthread create error\n");
						   goto err;
					   }
	}
	
	/* 3. Attendre la fin du traitement. */
	for(int i = 0; i < nb_thread; i++)
	{
		pthread_join(threads[i],NULL);
	}
	

	/* 4. Fusion des pièces.
	 *
	 * La fonction piece_merge est disponible afin d'accomplir ceci.
	 * Notez bien que les pièces ayant la même orientation initiale
	 * doivent être fusionnées ensemble.
	 * */
	for (int i = 0; i < NB_TILES; i++) {
		for(int j = 0; j< nb_thread; j++){
			piece_merge(&masters[i], thread_data[j].pieces[i], tiles_orientation[i]);
		}
	}

	/* La limite globale est calculée à partir des limites
	 * de chaque dragon calculées à l'étape 4.
	 */
	merge_limits(&masters[0].limits, &masters[1].limits);
	merge_limits(&masters[0].limits, &masters[2].limits);
	merge_limits(&masters[0].limits, &masters[3].limits);

done:
	FREE(threads);
	FREE(thread_data);
	*limits = masters[0].limits;
	return ret;
err:
	ret = -1;
	goto done;
}
