/*
 i* dragon_tbb.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#include <iostream>

extern "C" {
#include "dragon.h"
#include "color.h"
#include "utils.h"
}
#include "dragon_tbb.h"
#include "tbb/tbb.h"
#include "TidMap.h"

using namespace std;
using namespace tbb;

static TidMap* tid = NULL;

class DragonLimits {
	public:
	piece_t pieces[NB_TILES];

	DragonLimits(){
		for(int i =0; i< NB_TILES; i++){
			piece_init(&pieces[i]);
			pieces[i].orientation = tiles_orientation[i];
		}
	}

	DragonLimits(const DragonLimits& p, split){
		for(int i =0; i< NB_TILES; i++){
			piece_init(&pieces[i]);
			pieces[i].orientation = tiles_orientation[i];
		}
	}

	void operator()(const blocked_range<int>& range){		
		for(int i =0; i< NB_TILES; i++){
			piece_limit(range.begin(), range.end(), &pieces[i]);
		}
	}

	void join(DragonLimits& p){
		for(int i =0; i< NB_TILES; i++){
			piece_merge(&pieces[i], p.pieces[i], tiles_orientation[i]);
		}
	}

};

class DragonDraw {
	public:
	struct draw_data& info;
	int index;

	DragonDraw(struct draw_data* data)
	: info(*data), index (0)
	{}
	DragonDraw(const DragonDraw& drgL)
	: info(drgL.info)
	, index(drgL.index+1)
	{
		// L'index est laissé pseudo aléatoire pour donner des couleurs différentes 
		// Indexation de l'ID
		index = tid->getIdFromTid(gettid());
	}

	void operator()(const blocked_range<uint64_t>& range) const{
		for(int tile =0; tile < NB_TILES; tile++){
			if(dragon_draw_raw(tile, range.begin(), range.end(), info.dragon, info.dragon_width, info.dragon_height, info.limits, index) < 0) {
				printf("2begin: %ld, end: %ld\n", range.begin(), range.end());
			}
		}
			//printf("tid is %d, interval is %ld to %ld\n", gettid(), range.begin(), range.end());
	}
	
};

class DragonRender {
	public:
	struct draw_data& info;

	DragonRender(struct draw_data* data)
	: info(*data)
	{}
	DragonRender(const DragonRender& drgR)
	: info(drgR.info)
	{}

	void operator()(const blocked_range<int>& range) const{
		scale_dragon(range.begin(), range.end(), info.image, info.image_width, info.image_height, info.dragon, info.dragon_width, info.dragon_height, info.palette);
	}
};

class DragonClear {
	public:
	char value;
	char *canvas;

	DragonClear(char initValue, char *initCanvas)
	: value(initValue), canvas(initCanvas)
	{}

	DragonClear(const DragonClear& drgC)
	: value(drgC.value)
	, canvas(drgC.canvas)
	{} 

	void operator()(const blocked_range<int>& range) const{
		init_canvas(range.begin(), range.end(), canvas, value);
	}
};

int dragon_draw_tbb(char **canvas, struct rgb *image, int width, int height, uint64_t size, int nb_thread)
{
	tid = new TidMap(nb_thread);

	//TODO("dragon_draw_tbb");
	struct draw_data data;
	limits_t limits;
	char *dragon = NULL;
	int dragon_width;
	int dragon_height;
	int dragon_surface;
	int scale_x;
	int scale_y;
	int scale;
	int deltaJ;
	int deltaI;
	struct palette *palette = init_palette(nb_thread);
	if (palette == NULL)
		return -1;

	/* 1. Calculer les limites du dragon */
	dragon_limits_tbb(&limits, size, nb_thread);

	dragon_width = limits.maximums.x - limits.minimums.x;
	dragon_height = limits.maximums.y - limits.minimums.y;
	dragon_surface = dragon_width * dragon_height;
	scale_x = dragon_width / width + 1;
	scale_y = dragon_height / height + 1;
	scale = (scale_x > scale_y ? scale_x : scale_y);
	deltaJ = (scale * width - dragon_width) / 2;
	deltaI = (scale * height - dragon_height) / 2;

	dragon = (char *) malloc(dragon_surface);
	if (dragon == NULL) {
		free_palette(palette);
		return -1;
	}

	data.nb_thread = nb_thread;
	data.dragon = dragon;
	data.image = image;
	data.size = size;
	data.image_height = height;
	data.image_width = width;
	data.dragon_width = dragon_width;
	data.dragon_height = dragon_height;
	data.limits = limits;
	data.scale = scale;
	data.deltaI = deltaI;
	data.deltaJ = deltaJ;
	data.palette = palette;
	data.tid = (int *) calloc(nb_thread, sizeof(int));

	task_scheduler_init init(nb_thread);

	/* 2. Initialiser la surface : DragonClear */
	DragonClear clear(-1, dragon);
	parallel_for(blocked_range<int>(0,dragon_surface), clear);

	/* 3. Dessiner le dragon : DragonDraw */
	DragonDraw draw(&data);
	parallel_for(blocked_range<uint64_t>(0,data.size), draw);

	/* 4. Effectuer le rendu final */
	DragonRender render(&data);
	parallel_for(blocked_range<int>(0,height), render);

	init.terminate();
	free_palette(palette);
	FREE(data.tid);
	*canvas = dragon;
	tid->dump();
	delete tid;
	return 0;
}

/*
 * Calcule les limites en terme de largeur et de hauteur de
 * la forme du dragon. Requis pour allouer la matrice de dessin.
 */
int dragon_limits_tbb(limits_t *limits, uint64_t size, int nb_thread)
{
	//TODO("dragon_limits_tbb");
	DragonLimits lim;

	/* 1. Calculer les limites */
	task_scheduler_init init(nb_thread);
	//printf("%d\n", nb_thread);
	parallel_reduce(blocked_range<int>(0,size), lim);

	/* La limite globale est calculée à partir des limites
	 * de chaque dragon.
	 */
	merge_limits(&lim.pieces[0].limits, &lim.pieces[1].limits);
	merge_limits(&lim.pieces[0].limits, &lim.pieces[2].limits);
	merge_limits(&lim.pieces[0].limits, &lim.pieces[3].limits);

	*limits = lim.pieces[0].limits;
	return 0;
}
