/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# (c) 2005 Naomi Peori <naomi@peori.ca>
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
#
*/

#include <kernel.h>
#include <stdlib.h>
#include <malloc.h>
#include <tamtypes.h>
#include <math3d.h>
#include <string.h>
#include <packet.h>

#include <dma_tags.h>
#include <gif_tags.h>
#include <gs_psm.h>

#include <dma.h>

#include <graph.h>

#include <draw.h>
#include <draw3d.h>

#include "mesh_data.c"
#include "pad.c"

VECTOR object_position = { 0.00f, 0.00f, 0.00f, 1.00f };
VECTOR object_rotation = { 0.00f, 0.2f, 0.00f, 1.00f };

VECTOR camera_position = { 0.00f, 0.00f, 100.00f, 1.00f };
VECTOR camera_rotation = { 0.00f, 0.00f,   0.00f, 1.00f };

void get_pad_input(u32 * ret, int port, int slot, char * inputString);

void init_gs(framebuffer_t *frame, zbuffer_t *z)
{

	// Define a 32-bit 640x512 framebuffer.
	frame->width = 640;
	frame->height = 512;
	frame->mask = 0;
	frame->psm = GS_PSM_32;
	frame->address = graph_vram_allocate(frame->width,frame->height, frame->psm, GRAPH_ALIGN_PAGE);

	// Enable the zbuffer.
	z->enable = DRAW_ENABLE;
	z->mask = 0;
	z->method = ZTEST_METHOD_GREATER_EQUAL;
	z->zsm = GS_ZBUF_32;
	z->address = graph_vram_allocate(frame->width,frame->height,z->zsm, GRAPH_ALIGN_PAGE);

	// Initialize the screen and tie the first framebuffer to the read circuits.
	graph_initialize(frame->address,frame->width,frame->height,frame->psm,0,0);

}

void init_drawing_environment(framebuffer_t *frame, zbuffer_t *z)
{

	packet_t *packet = packet_init(16,PACKET_NORMAL);

	// This is our generic qword pointer.
	qword_t *q = packet->data;

	// This will setup a default drawing environment.
	q = draw_setup_environment(q,0,frame,z);

	// Now reset the primitive origin to 2048-width/2,2048-height/2.
	q = draw_primitive_xyoffset(q,0,(2048-320),(2048-256));

	// Finish setting up the environment.
	q = draw_finish(q);

	// Now send the packet, no need to wait since it's the first.
	dma_channel_send_normal(DMA_CHANNEL_GIF,packet->data,q - packet->data, 0, 0);
	dma_wait_fast();

	packet_free(packet);

}

int render(framebuffer_t *frame, zbuffer_t *z, u32 * ret)
{

	int i;
	int context = 0;

	// Matrices to setup the 3D environment and camera
	MATRIX local_world;
	MATRIX world_view;
	MATRIX view_screen;
	MATRIX local_screen;

	VECTOR *temp_vertices;

	prim_t prim;
	color_t color;

	xyz_t   *verts;
	color_t *colors;

	// The data packets for double buffering dma sends.
	packet_t *packets[2];
	packet_t *current;
	qword_t *dmatag;

	packets[0] = packet_init(100,PACKET_NORMAL);
	packets[1] = packet_init(100,PACKET_NORMAL);

	// Allocate calculation space.
	temp_vertices = memalign(128, sizeof(VECTOR) * vertex_count);

	// Allocate register space.
	verts  = memalign(128, sizeof(vertex_t) * vertex_count);
	colors = memalign(128, sizeof(color_t)  * vertex_count);

	// Define the triangle primitive we want to use.
	prim.type = PRIM_TRIANGLE;
	prim.shading = PRIM_SHADE_FLAT;
	prim.mapping = DRAW_DISABLE;
	prim.fogging = DRAW_DISABLE;
	prim.blending = DRAW_DISABLE;
	prim.antialiasing = DRAW_ENABLE;
	prim.mapping_type = PRIM_MAP_ST;
	prim.colorfix = PRIM_UNFIXED;

	color.r = 0x80;
	color.g = 0x80;
	color.b = 0x80;
	color.a = 0x80;
	color.q = 1.0f;

	// Create the view_screen matrix.
	create_view_screen(view_screen, graph_aspect_ratio(), -3.00f, 3.00f, -3.00f, 3.00f, 1.00f, 2000.00f);

	// Wait for any previous dma transfers to finish before starting.
	dma_wait_fast();

	// The main loop...
	for (;;)
	{
		char input[20] = "";
		get_pad_input(ret, 0, 0, input);
		//printf("Pad input is %s", input);

		if(strcmp(input, "NULL") != 0)
		{
			printf("Pad input is %s", input);
			if(strstr(input, "UP") != NULL)
			{
				object_position[1] += 0.2f;
			}
			if(strcmp(input, "DOWN") == 0)
			{
				object_position[1] -= 0.2f;
			}
			if(strcmp(input, "LEFT") == 0)
			{
				object_position[0] -= 0.2f;
			}
			if(strcmp(input, "RIGHT") == 0)
			{
				object_position[0] += 0.2f;
			}
		}

		

		qword_t *q;

		current = packets[context];

		// Spin the cube a bit.
		//object_rotation[0] += 0.8f; //while (object_rotation[0] > 3.14f) { object_rotation[0] -= 6.28f; }
		//object_rotation[1] += 0.2f; //while (object_rotation[1] > 3.14f) { object_rotation[1] -= 6.28f; }

		// Create the local_world matrix.
		create_local_world(local_world, object_position, object_rotation);

		// Create the world_view matrix.
		create_world_view(world_view, camera_position, camera_rotation);

		// Create the local_screen matrix.
		create_local_screen(local_screen, local_world, world_view, view_screen);

		// Calculate the vertex values.
		calculate_vertices(temp_vertices, vertex_count, vertices, local_screen);

		// Convert floating point vertices to fixed point and translate to center of screen.
		draw_convert_xyz(verts, 2048, 2048, 32, vertex_count, (vertex_f_t*)temp_vertices);

		// Convert floating point colours to fixed point.
		draw_convert_rgbq(colors, vertex_count, (vertex_f_t*)temp_vertices, (color_f_t*)colours, 0x80);

		// Grab our dmatag pointer for the dma chain.
		dmatag = current->data;

		// Now grab our qword pointer and increment past the dmatag.
		q = dmatag;
		q++;

		// Clear framebuffer but don't update zbuffer.
		q = draw_disable_tests(q,0,z);
		q = draw_clear(q,0,2048.0f-320.0f,2048.0f-256.0f,frame->width,frame->height,0x00,0x00,0x00);
		q = draw_enable_tests(q,0,z);

		// Draw the triangles using triangle primitive type.
		q = draw_prim_start(q,0,&prim, &color);

		for(i = 0; i < points_count; i++)
		{
			q->dw[0] = colors[points[i]].rgbaq;
			q->dw[1] = verts[points[i]].xyz;
			q++;
		}

		q = draw_prim_end(q,2,DRAW_RGBAQ_REGLIST);

		// Setup a finish event.
		q = draw_finish(q);

		// Define our dmatag for the dma chain.
		DMATAG_END(dmatag,(q-current->data)-1,0,0,0);

		// Now send our current dma chain.
		dma_wait_fast();
		dma_channel_send_chain(DMA_CHANNEL_GIF,current->data, q - current->data, 0, 0);

		// Now switch our packets so we can process data while the DMAC is working.
		context ^= 1;

		// Wait for scene to finish drawing
		draw_wait_finish();

		graph_wait_vsync();

	}

	packet_free(packets[0]);
	packet_free(packets[1]);

	return 0;

}

//Returns last pad value as string
void get_pad_input(u32 * ret, int port, int slot, char * inputString)
{
    struct padButtonStatus buttons;
    u32 paddata;
    u32 old_pad = 0;
    u32 new_pad;
	

	// SifInitRpc(0);

	// loadModules();

    // padInit(0);

	// port = 0; // 0 -> Connector 1, 1 -> Connector 2
    // slot = 0; // Always zero if not using multitap
		


    // if((ret = padPortOpen(port, slot, padBuf)) == 0) 
	// {
	// 	return "Pad port not open.";
    // }

    // if(!initializePad(port, slot)) 
	// {
    //     return "Pad failed to initialize";
    // }



	// ret = padGetState(port, slot);
	
	// if (ret == PAD_STATE_DISCONN)
	// { 
	// 	return "Pad is diconnected.";
	// }

	*ret = padRead(port, slot, &buttons); // port, slot, buttons

	if (ret != 0)
	{
		paddata = 0xffff ^ buttons.btns;

		new_pad = paddata & ~old_pad;
		old_pad = paddata;

		// Directions
		if (new_pad & PAD_LEFT)
		{
			strcat(inputString, "LEFT");
		}
		if (new_pad & PAD_DOWN)
		{
			strcat(inputString, "DOWN");	
		}
		if (new_pad & PAD_RIGHT)
		{
			strcat(inputString, "RIGHT");
		}
		if (new_pad & PAD_UP)
		{
			strcat(inputString, "UP");	
		}
		if (new_pad & PAD_START)
		{
			//return "START";
		}
		if (new_pad & PAD_R3)
		{
			//return "R3";		
		}
		if (new_pad & PAD_L3)
		{
			//return "L3";		
		}
		if (new_pad & PAD_SELECT)
		{
			//return "SELECT";
		}
		if (new_pad & PAD_SQUARE)
		{
			//return "SQUARE";
		}
		if (new_pad & PAD_CROSS)
		{
			
			//return "CROSS";
		}
		if (new_pad & PAD_CIRCLE)
		{
			
			//return "CIRCLE";
		}
		if (new_pad & PAD_TRIANGLE)
		{
			
			//return "TRIANGLE";
		}
		if (new_pad & PAD_R1)
		{
			//return "R1";
		}
		if (new_pad & PAD_L1)
		{

			//return"L1";
		}
		if (new_pad & PAD_R2)
		{
			//return"R2";		
		}
		if (new_pad & PAD_L2)
		{
			//return"L2";		
		}
		// else
		// {
		// 	strcpy(inputString, "NULL");
		// }

		
	}
}

int main(int argc, char *argv[])
{

	// The buffers to be used.
	framebuffer_t frame;
	zbuffer_t z;
	int port, slot;
	u32 ret;

	// Init GIF dma channel.
	dma_channel_initialize(DMA_CHANNEL_GIF,NULL,0);
	dma_channel_fast_waits(DMA_CHANNEL_GIF);

	// Init the GS, framebuffer, and zbuffer.
	init_gs(&frame, &z);

	// Init the drawing environment and framebuffer.
	init_drawing_environment(&frame,&z);

	SifInitRpc(0);

	loadModules();

    padInit(0);

	port = 0; // 0 -> Connector 1, 1 -> Connector 2
    slot = 0; // Always zero if not using multitap
		


    if((ret = padPortOpen(port, slot, padBuf)) == 0) 
	{
		printf( "Pad port not open.");
    }

    if(!initializePad(port, slot)) 
	{
        printf("Pad failed to initialize");
    }



	ret = padGetState(port, slot);
	
	if (ret == PAD_STATE_DISCONN)
	{ 
		printf( "Pad is diconnected.");
	}

	// Render the cube
	render(&frame,&z, &ret);

	// Sleep
	SleepThread();

	// End program.
	return 0;

}
