// Edward Sillador
// 05/16/2018
// * 05/16/2018 - implement image resize
// * 03/02/2018 - fixed crash in getNextPixel()
//                index to the input file buffer exceeds the image file size
//                getNextPixel() now returns error value.
// * 03/01/2018 - the following are changes:
// *              1. Added name and date
// *              2. Format tab width from 2 to 4
// *              3. Added argument in getNextToken(). This will return error value and
// *                 the newly added parameter current_token will be the output of the
// *                 function. 
// *              4. Return int in doProcessPPM() for error value.
// *              5. Return int in rotateGrayScaleImage() for error value.
// *              6. Added error checking in the functions:
// *                 - rotateGrayScaleImage
// *                 - putImageToFile
// *                 - getImageInfo
// *                 - doProcessPPM
// *              7. !Change! the rotation direction.
// * 12/2017 - initial version

//#define TEST_RESIZE
//#define TEST_ROTATE
#define TEST_COMARGS

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#define DATA_BUFLEN 10
#define MAX_HEADER_CHARS 128

#define PPM_MAGIC_NUM 0
#define PPM_UNSIGNED 1
#define PPM_EOF -1
#define PPM_ERROR -2

#define FILETYPE_PPM 0
#define FILETYPE_PGM 1
#define FILETYPE_PBM 2

typedef struct img_scale_info {
    int input_size;
    int output_size;
    float scale;
    int kernel_width;
    float **weights;
    int **indices;
    int P;
    int num_non_zero;
} img_scale_info;

typedef struct img_rotate_info {
    float angle;
} img_rotate_info;

typedef struct pixel {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} pixel;
  
typedef struct img_info {
    unsigned int height;
    unsigned int width;
    unsigned int new_height;
    unsigned int new_width;
    unsigned int max_color;
    unsigned int size;
    unsigned int file_type;
    pixel **buff;
    pixel **new_buff;
} img_info;

typedef struct args_flag {
    char resize_enable;
    char rotate_enable;
	char flipv_enable;
	char fliph_enable;
	char gray_enable;
	char mono_enable;
} args_flag;

// this structure is used for parsing the header file
typedef struct token{
    int kind;
    char data[DATA_BUFLEN];
    char current_char;
} token;

// contains PPM image information
typedef struct ppm_image_handler {
    img_info imginfo;
    args_flag arg_flag;
    img_scale_info scale_info;
    FILE *filep;
    unsigned char* file_buffer;
    char *filename;
    unsigned int filesize;
    unsigned int index_buffer;
    token tkn;
    img_rotate_info rotate_info;
} ppm_image_handler;

int calc_contributions(img_scale_info *scale_info);
int mod(int a, int b);
float cubic(float x);
void init_img_scale_info(ppm_image_handler *handler);
void release_scale_info(img_scale_info *scale_info);

int getNextToken(ppm_image_handler *handler, token *current_token);
void getNextChar(ppm_image_handler *handler);
int getNextPixel(ppm_image_handler *handler, pixel *pix);
int doProcessPPM(ppm_image_handler *handler);
int getImageInfo(ppm_image_handler *handler);
int rotateGrayScaleImage(ppm_image_handler *handler); // this can be removed
void releaseBuffer(ppm_image_handler *handler); // this can be simplified without using handler
int buffalloc(pixel ***new_buff, unsigned int height, unsigned int width);

void usage()
{
	printf("ppmx-edward [options] (input filename)\n");
	printf("Options -fv  Flip vertically\n");
	printf("        -fh  Flip horizontally\n");
	printf("        -w(new width) Scale to the new width\n");
	printf("        -w100 means new width is 100\n");
	printf("        -r(angle)  Rotate (CW)\n");
	printf("        -r30 means rotate 30 degree CW.\n");
	printf("        -mono Convert to bilevel (.pbm) format\n");
	printf("        -gray  Convert to grayscale (.pgm) format\n");
}

int main(int argc, char **argv)
{
	ppm_image_handler handler;
	int x;

	handler.arg_flag.rotate_enable = 0;
	handler.arg_flag.resize_enable = 0;
    handler.arg_flag.gray_enable = 0;
	handler.arg_flag.flipv_enable = 0;
	handler.arg_flag.fliph_enable = 0;
    handler.arg_flag.mono_enable = 0;
	handler.filename = NULL;

	for (x = 1; x < argc; x++)
	{
		if (argv[x][0] == '-') {
			if (argv[x][1] == 'f') {
				if (argv[x][2] == 'h')
				{
                    handler.arg_flag.fliph_enable = 1;
				}
				else if (argv[x][2] == 'v')
				{
                    handler.arg_flag.flipv_enable = 1;
				}
				else
				{
					printf("invalid option for flip.\nallowed options are -fh -fv only.\n");
					exit(0);
				}
			}
			else if (argv[x][1] == 'w')
			{
				handler.arg_flag.resize_enable = 1;
				handler.scale_info.output_size = (int) atoi(&argv[x][2]);
			}
			else if (argv[x][1] == 'r')
			{
				handler.arg_flag.rotate_enable = 1;
				handler.rotate_info.angle = (float) atoi(&argv[x][2]);
                if (handler.rotate_info.angle < 0 || handler.rotate_info.angle >= 360)
                {
                    printf("invalid option for rotate. it is less than 0 or greater than 359\n");
                    exit(0);
                }
                
				if (handler.rotate_info.angle == 0)
				{
					printf("invalid option of rotate.\nallowed option is -r<degrees>.\n");
					exit(0);
				}
			}
            else if (strcmp(&argv[x][1], "gray") == 0)
            {
                handler.arg_flag.gray_enable = 1;
            }
			else if (strcmp(&argv[x][1], "mono") == 0)
			{
                handler.arg_flag.mono_enable = 1;
			}
		}
		else
		{
			handler.filename = &argv[x][0];
		}
	}
	if (handler.filename == NULL)
	{
		usage();
		exit(0);
	}
    if (doProcessPPM(&handler) != 0) return -1;

    return 0;
}

// write output image to file
int putImageToFile(ppm_image_handler *handler)
{
    int fileout_size =  strlen(handler->filename) + 5;
    char fileout[MAX_HEADER_CHARS];
    FILE *fofp;
    unsigned int x;
    unsigned int y;
    int error = 0;

    memset(fileout, '\0', MAX_HEADER_CHARS);
    strncpy(fileout, handler->filename, fileout_size);
    strcat(fileout, ".out");

    if ((fofp = fopen(fileout, "wb")) == NULL)
    {
        printf("unable to open file for writing: %s\n", fileout);
        return -1;
    }

    if (handler->imginfo.file_type == FILETYPE_PGM)
    {
        error = fwrite("P5\n", 1, 3, fofp);
        if (error != 3)
        {
            printf("failed in writing to %s\n", fileout);
            return -1;
        }
    }
    else if (handler->imginfo.file_type == FILETYPE_PBM)
    {
        error = fwrite("P4\n", 1, 3, fofp);
        if (error != 3)
        {
            printf("failed in writing to %s\n", fileout);
            return -1;
        }
    }
    else
    {
        error = fwrite("P6\n", 1, 3, fofp);
        if (error != 3)
        {
            printf("failed in writing to %s\n", fileout);
            return -1;
        }
    }

    sprintf(fileout, "%s", "# generated by ppmx_edward\n");
    error = fwrite(fileout, 1, strlen(fileout), fofp);
    if (error != strlen(fileout))
    {
        printf("failed in writing to %s\n", fileout);
        return -1;
    }

    sprintf(fileout, "%u ", handler->imginfo.new_width);
    error = fwrite(fileout, 1, strlen(fileout), fofp);
    if (error != strlen(fileout))
    {
        printf("failed in writing to %s\n", fileout);
        return -1;
    }

    sprintf(fileout, "%u\n", handler->imginfo.new_height);
    error = fwrite(fileout, 1, strlen(fileout), fofp);
    if (error != strlen(fileout))
    {
        printf("failed in writing to %s\n", fileout);
        return -1;
    }

    if (!(handler->imginfo.file_type == FILETYPE_PBM))
    {
        sprintf(fileout, "%u\n", handler->imginfo.max_color);
        error = fwrite(fileout, 1, strlen(fileout), fofp);
        if (error != strlen(fileout))
        {
            printf("failed in writing to %s\n", fileout);
            return -1;
        }
    }

    if (handler->imginfo.file_type == FILETYPE_PGM)
    {
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                if ((error = fwrite(&handler->imginfo.new_buff[y][x].r,1, 1, fofp)) != 1)
                {
                    printf("failed in writing to %s\n", fileout);
                    return -1;
                }
            }
    }
    else if (handler->imginfo.file_type == FILETYPE_PBM)
    {
        unsigned char tmp = 0;
        int z = 0;
        for (y = 0; y < handler->imginfo.new_height; y++)
        {
            for (x = 0, tmp = 1, z = 0; x < (handler->imginfo.new_width); x++, tmp++)
            {
                z = z | (handler->imginfo.new_buff[y][x].r << (8 - tmp));
                if (tmp % 8 == 0)
                {
                    if ((error = fwrite(&z,1, 1, fofp)) != 1)
                    {
                        printf("failed in writing to %s\n", fileout);
                        return -1;
                    }
                    tmp = 0;
                    z = 0;
                }
            }
            if (tmp-1 != 0)
            {
                if ((error = fwrite(&z,1, 1, fofp)) != 1)
                {
                    printf("failed in writing to %s\n", fileout);
                    return -1;
                }
            }
        }
    }
    else
    {
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                if ((error = fwrite(&handler->imginfo.new_buff[y][x].r,1, 1, fofp)) != 1)
                {
                    printf("failed in writing to %s\n", fileout);
                    return -1;
                }
                if ((error = fwrite(&handler->imginfo.new_buff[y][x].g,1, 1, fofp)) != 1)
                {
                    printf("failed in writing to %s\n", fileout);
                    return -1;
                }
                if ((error = fwrite(&handler->imginfo.new_buff[y][x].b,1, 1, fofp)) != 1)
                {
                    printf("failed in writing to %s\n", fileout);
                    return -1;
                }
            }
    }

    for (y = 0; y < handler->imginfo.new_height; y++)
        free(handler->imginfo.new_buff[y]);

    fclose(fofp);

    if (handler->imginfo.new_height != 0 || handler->imginfo.new_height != 0) free(handler->imginfo.new_buff);

    return 0;
}

int getNextPixel(ppm_image_handler *handler, pixel *pix)
{
    pixel ret; // can be deleted to optimize lines

    if (handler->index_buffer > handler->filesize)
    {
        printf("error. unexpected end of file. file index: %0d exceeds file size: %0d\n", handler->index_buffer, handler->filesize);
        return -1;
    }
    ret.r = handler->file_buffer[handler->index_buffer++];
    ret.g = handler->file_buffer[handler->index_buffer++];
    ret.b = handler->file_buffer[handler->index_buffer++];

    *pix = ret;
    return 0;
}

void getNextChar(ppm_image_handler *handler)
{
    if (handler->tkn.current_char == EOF) return;

    if (handler->index_buffer < handler->filesize) handler->tkn.current_char = handler->file_buffer[handler->index_buffer++];
    else handler->tkn.current_char = EOF;

    // ignore comments
    if (handler->tkn.current_char == '#')
    {
        do
        {
            handler->tkn.current_char = handler->file_buffer[handler->index_buffer++];
        } while (handler->tkn.current_char != '\n');
        handler->tkn.current_char = '\n';
    }
}

int getNextToken(ppm_image_handler *handler, token *current_token)
{
    // returns error for any invalid token
    handler->tkn.kind = PPM_ERROR;

    // ignore spaces
    while (isspace(handler->tkn.current_char)) getNextChar(handler);

    // if token is a digit
    if (isdigit(handler->tkn.current_char))
    {
        int index = 0;
        memset(&(handler->tkn.data), '\0', DATA_BUFLEN);
        do
        {
            handler->tkn.data[index++] = handler->tkn.current_char;
            getNextChar(handler);
        } while (isdigit(handler->tkn.current_char));
        handler->tkn.kind = PPM_UNSIGNED;
    }
    else if (isalpha(handler->tkn.current_char))
    { // if token is a word
        int index = 0;
        do {
            handler->tkn.data[index++] = handler->tkn.current_char;
            getNextChar(handler);
        } while (isalnum(handler->tkn.current_char));
        handler->tkn.data[index] = '\0';

        if ((strncmp(handler->tkn.data, "P6", DATA_BUFLEN)) == 0)
        {
            handler->tkn.kind = PPM_MAGIC_NUM;
        }
        getNextChar(handler);
    }
    else return -1; // return error for anything else

    *current_token = handler->tkn;
    return 0;
}

// converts the buffer to 2 dimensional image
int getImageInfo(ppm_image_handler *handler)
{
    token current_token;
    unsigned int x;
    unsigned int y;
    int error = 0;

    // retrieve the magic number
    if ((error = getNextToken(handler, &current_token)) != 0)
    {
        printf("error in getting next token. wrong format.\n");
        return -1;
    }
    if (current_token.kind != PPM_MAGIC_NUM)
    {
        printf("error. invalid file format.\n");
        return -1;
    }
    handler->imginfo.file_type = FILETYPE_PPM;

    // retrieve the width
    if ((error = getNextToken(handler, &current_token)) != 0)
    {
        printf("error in getting next token. wrong format.\n");
        return -1;
    }
    if (current_token.kind != PPM_UNSIGNED)
    {
        printf("error. invalid file format. unable to parse width from input file.\n");
        return -1;
    }
    handler->imginfo.width = atoi(current_token.data);

    // retrieve the height
    if ((error = getNextToken(handler, &current_token)) != 0)
    {
        printf("error in getting next token. wrong format.\n");
        return -1;
    }
    if (current_token.kind != PPM_UNSIGNED)
    {
        printf("error. invalid file format. unable to parse height from input file.\n");
        return -1;
    }
    handler->imginfo.height = atoi(current_token.data);

    // retrieve the maximum color
    if ((error = getNextToken(handler, &current_token)) != 0)
    {
        printf("error in getting next token. wrong format.\n");
        return -1;
    }
    if (current_token.kind != PPM_UNSIGNED)
    {
        printf("error. invalid file format. unable to parse maximum color from input file.\n");
        return -1;
    }
    handler->imginfo.max_color = atoi(current_token.data);
  
    handler->imginfo.buff = (pixel **) malloc(handler->imginfo.height * sizeof(pixel *));
    if (handler->imginfo.buff == NULL)
    {
        printf("error. can not allocate memory\n");
        return -1;
    }
  
    handler->imginfo.size = handler->imginfo.height * handler->imginfo.width;
    for (y = 0; y < handler->imginfo.height; y++)
    {
        handler->imginfo.buff[y] = (pixel *) malloc(handler->imginfo.width * sizeof(pixel));
        if (handler->imginfo.buff[y] == NULL)
        {
            printf("error. can not allocate memory\n");
            return -1;
        }
        for (x = 0; x < handler->imginfo.width; x++)
        {
            if (getNextPixel(handler, &handler->imginfo.buff[y][x]) != 0) return -1;
        }
    }
    handler->imginfo.new_width = 0;
    handler->imginfo.new_height = 0;

    if (handler->filesize != handler->index_buffer)
    {
        printf("file format error\n");
        return -1;
    }

    return 0;
}

void releaseBuffer(ppm_image_handler *handler)
{
    unsigned int y;
    for (y = 0; y < handler->imginfo.height; y++)
        free(handler->imginfo.buff[y]);

    free(handler->imginfo.buff);
}

float cubic(float x)
{
    float ret = 0;
    float absx = fabs(x);
    float absx2 = absx * absx;
    float absx3 = absx2 * absx;

    if (absx < 1) ret = (1.5*absx3) - (2.5*absx2) + 1;

    if ((1 < absx) && (absx <= 2)) ret = ret + ((-0.5*absx3) + (2.5*absx2) - (4*absx) + 2);
            
    return ret;
}

int mod(int a, int b)
{
    int r = 0;
    if (b != 0) r = a % b;
    return r < 0 ? r + b : r;
}

int calc_contributions(img_scale_info *scale_info)
{
    int x = 0;
    int y = 0;
    unsigned int aux_size = scale_info->input_size * 2;
    int *aux;
    float **indices;
    float **weights;

    indices = (float **) malloc(scale_info->output_size * sizeof(float*));
    weights = (float **) malloc(scale_info->output_size * sizeof(float*));
    if (indices == NULL || weights == NULL)
    {
        printf("fatal. allocating indices\n");
        return - 1;
    }

    for (y = 0; y < scale_info->output_size; y++)
    {
        indices[y] = (float *) malloc(scale_info->P * sizeof(float));
        weights[y] = (float *) malloc(scale_info->P * sizeof(float));

        if (indices[y] == NULL || weights[y] == NULL)
        {
            printf("fatal. allocating memory for weights and indices");
            return -1;
        }
    }

    if ((aux = (int *) malloc(aux_size * (sizeof(int)))) == NULL)
    {
        printf("fatal. allocating memory for aux\n");
        return - 1;
    }            
    memset(aux, 0, aux_size);
    y = 0;
    for (x = 0; x < scale_info->input_size; x++)
        aux[y++] = x;

    for (x = scale_info->input_size-1; x >= 0; x--)
        aux[y++] = x;

    // generate indices
    for (y = 0; y < scale_info->output_size; y++)
    {
        for (x = 0; x < scale_info->P; x++)
        {
            // generate an array from 1 to output_size
            // divide each element to scale
            float u = ((y+1) / scale_info->scale) + (0.5 * (1 - (1 / scale_info->scale)));
            indices[y][x] = floor(u - (scale_info->kernel_width / 2)) + (x - 1);
        }
    }

    // generate weights
    for (y = 0; y < scale_info->output_size; y++)
        for (x = 0; x < scale_info->P; x++)
        {
            float u = ((y+1) / scale_info->scale) + (0.5 * (1 - (1 / scale_info->scale)));
            weights[y][x] = cubic(u - indices[y][x] - 1);
        }

    for (y = 0; y < scale_info->output_size; y++)
        for (x = 0; x < scale_info->P; x++)
            indices[y][x] = aux[mod(((int) indices[y][x]),aux_size)];

    scale_info->num_non_zero = 0;
    for (x = 0; x < scale_info->P; x++)
        if (weights[0][x] != 0.0f)
            scale_info->num_non_zero = scale_info->num_non_zero + 1;

    scale_info->indices = (int **) malloc(scale_info->output_size * sizeof(int*));
    scale_info->weights = (float **) malloc(scale_info->output_size * sizeof(float*));
    
    for (y = 0; y < scale_info->output_size; y++)
    {
        scale_info->indices[y] = (int *) malloc(scale_info->num_non_zero * sizeof(int));
        scale_info->weights[y] = (float *) malloc(scale_info->num_non_zero * sizeof(float));
    }

    int ind_w_ptr_x = 0;
    for (y = 0; y < scale_info->output_size; y++)
    {
        ind_w_ptr_x = 0;
        for (x = 0; x < scale_info->P; x++)
            if (weights[y][x] != 0.0f)
            {
                scale_info->indices[y][ind_w_ptr_x] = indices[y][x];
                scale_info->weights[y][ind_w_ptr_x] = weights[y][x];
                ind_w_ptr_x = ind_w_ptr_x + 1;
            }
    }

    for (y = 0; y < scale_info->output_size; y++)
    {
        free(indices[y]);
        free(weights[y]);
    }
	
    free(weights);
    free(indices);
    free(aux);

    return 0;
}

void release_scale_info(img_scale_info *scale_info)
{
    int y = 0;
    for (y = 0; y < scale_info->output_size; y++)
    {
        free(scale_info->weights[y]);
        free(scale_info->indices[y]);
    }
    free(scale_info->weights);
    free(scale_info->indices);
}

float getScaleFromSize(int in_size, int out_size)
{
    return ((float) out_size) / in_size;
}

void init_img_scale_info(ppm_image_handler *handler)
{
    handler->scale_info.kernel_width = 4.0;
    handler->scale_info.P = handler->scale_info.kernel_width + 2;
    handler->scale_info.input_size = handler->imginfo.width;
    handler->imginfo.new_height = handler->imginfo.height;
    //handler->scale_info.output_size = handler->imginfo.width / 2; // need to set from command line argument
    handler->scale_info.scale = getScaleFromSize(handler->scale_info.input_size, handler->scale_info.output_size);
}

float to_radians(float degrees) {
	return (degrees * M_PI)/180.0;
}

float to_degrees(float radians) {
    return (radians * 180.0) / M_PI;
}

void calc_rot_size(float angle,
				   unsigned int old_width, unsigned int old_height,
				   unsigned int *new_width, unsigned int *new_height)
{
    float theta1; // TODO: minimize lines here

    float a;
    float b;
    float c;
    float d;
    float e;
    float f;
	theta1 = to_radians(angle);
	d = old_height;
	c = old_width;

	b = c * sin(theta1);
	a = c * cos(theta1);
	f = d * sin(theta1);
	e = d * cos(theta1);
	*new_width = round(a + f);
	*new_height = round(b + e);
}	

int rotate(ppm_image_handler *handler)
{
    int x;
    int y;
    int x_center_in;
    int y_center_in;

	float angle;

	int x_offset;
	int y_offset;

	angle = handler->rotate_info.angle;

	if (angle >= 270) angle = 360 - angle;
	else if (angle > 180) angle = angle - 180;
	else if (angle > 90) angle = 180 - angle;

	calc_rot_size(angle, handler->imginfo.width, handler->imginfo.height, &handler->imginfo.new_width, &handler->imginfo.new_height);
	angle = to_radians(handler->rotate_info.angle);
	
	x_center_in = floor(handler->imginfo.width / 2);
	y_center_in = floor(handler->imginfo.height / 2);

	x_offset = floor(handler->imginfo.new_width / 2) - floor(handler->imginfo.width / 2);
	y_offset = floor(handler->imginfo.new_height / 2) - floor(handler->imginfo.height / 2);

    if (buffalloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == -1) return -1;

    for (y = 0; y < handler->imginfo.new_height; y++) memset(handler->imginfo.new_buff[y], 0x00, handler->imginfo.new_width * sizeof(pixel));

	for (y = 0; y < handler->imginfo.new_height; y++)
    {
        for (x = 0; x < handler->imginfo.new_width; x++)
        {
            float newX;
            float newY;
			int xx;
			int yy;

            int x0;
            int y0;

            float nX;
            float nY;

			xx = x - x_offset;
			yy = y - y_offset;
			x0 = xx - x_center_in;
			y0 = yy - y_center_in;

            newX = (cos(angle) * (float) (x0)) + (sin(angle) * (float) (y0));
            newY = -(sin(angle) * (float)(x0)) + (cos(angle) * (float) (y0));

            nX = (newX + x_center_in);
            nY = (newY + y_center_in);

			if ((nX < handler->imginfo.width) && (nY < handler->imginfo.height) && (nY >= 0) && (nX >= 0))
            {
                float q_r = 0.0f;
                float q_g = 0.0f;
                float q_b = 0.0f;
                int j;
                int i;

                if (nX > 1 && nY > 1 && nX < handler->imginfo.width - 3 && nY < handler->imginfo.height - 3)
                {
                    for (j = 0; j < 4; j++) 
                    {
                        int v = floor(nY) - 1 + j;
                        float p_r = 0.0f;
                        float p_g = 0.0f;
                        float p_b = 0.0f;
                        for (i = 0; i < 4; i++)
                        {
                            int u = floor(nX) - 1 + i;
                            p_r += (handler->imginfo.buff[v][u].r * cubic(nX - u));
                            p_g += (handler->imginfo.buff[v][u].g * cubic(nX - u));
                            p_b += (handler->imginfo.buff[v][u].b * cubic(nX - u));
                        }

                        q_r += p_r * cubic(nY - v);
                        q_g += p_g * cubic(nY - v);
                        q_b += p_b * cubic(nY - v);
                    }

                    if (q_r < 0) q_r = 0.0f;
                    if (q_g < 0) q_g = 0.0f;
                    if (q_b < 0) q_b = 0.0f;
                    
                    if (q_r >= 256) q_r = 255.0f;
                    if (q_g >= 256) q_g = 255.0f;
                    if (q_b >= 256) q_b = 255.0f;

                    handler->imginfo.new_buff[yy+y_offset][xx+x_offset].r = (int) q_r;
                    handler->imginfo.new_buff[yy+y_offset][xx+x_offset].g = (int) q_g;
                    handler->imginfo.new_buff[yy+y_offset][xx+x_offset].b = (int) q_b;
                }
                else
                {
                    handler->imginfo.new_buff[yy+y_offset][xx+x_offset] = handler->imginfo.buff[(int)nY][(int)nX];
                }
            }
        }
    }

    return 0;
}
        
int imresize(ppm_image_handler *handler)
{
    int x;
    int y;

    handler->imginfo.new_height = handler->imginfo.height;
    handler->imginfo.new_width = handler->scale_info.output_size;

    if (buffalloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == -1) return -1;
  
    for (y = 0; y < handler->imginfo.height; y++)
        for (x = 0; x < handler->imginfo.new_width; x++)
        {
            int z;
            float sum_r = 0.0f;
            float sum_g = 0.0f;
            float sum_b = 0.0f;

            for (z = 0; z < handler->scale_info.num_non_zero; z++)
            {
                int index = handler->scale_info.indices[x][z];

                sum_r = sum_r + handler->imginfo.buff[y][index].r * handler->scale_info.weights[x][z];
                sum_g = sum_g + handler->imginfo.buff[y][index].g * handler->scale_info.weights[x][z];
                sum_b = sum_b + handler->imginfo.buff[y][index].b * handler->scale_info.weights[x][z];
            }

            if (sum_r < 0.0f) sum_r = 0.0f;
            if (sum_g < 0.0f) sum_g = 0.0f;
            if (sum_b < 0.0f) sum_b = 0.0f;

            if (sum_r >= 256) sum_r = 255.0f;
            if (sum_g >= 256) sum_g = 255.0f;
            if (sum_b >= 256) sum_b = 255.0f;

            handler->imginfo.new_buff[y][x].r = (int) sum_r;
            handler->imginfo.new_buff[y][x].g = (int) sum_g;
            handler->imginfo.new_buff[y][x].b = (int) sum_b;
        }
    
    return 0;
}

// flip_direction - 1 vertical
//                - 0 horizontal
int flip(ppm_image_handler *handler, unsigned char flip_direction)
{
    int x;
    int y;

    handler->imginfo.new_height = handler->imginfo.height;
    handler->imginfo.new_width = handler->imginfo.width;

    handler->imginfo.new_buff = handler->imginfo.buff;

    handler->imginfo.new_buff[0][0].r = 0xff;
    handler->imginfo.new_buff[0][0].g = 0;
    handler->imginfo.new_buff[0][0].b = 0;
    if (flip_direction) // flip vertical
    {
        for (y = 0; y < handler->imginfo.new_height/2; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                pixel tmp = handler->imginfo.new_buff[y][x];
                handler->imginfo.new_buff[y][x] = handler->imginfo.new_buff[((handler->imginfo.new_height)-1) - y][x];
                handler->imginfo.new_buff[((handler->imginfo.new_height)-1) - y][x] = tmp;
            }
    }
    else
    {
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width/2; x++)
            {
                pixel tmp = handler->imginfo.new_buff[y][x];
                handler->imginfo.new_buff[y][x] = handler->imginfo.new_buff[y][((handler->imginfo.new_width)-1)-x];
                handler->imginfo.new_buff[y][((handler->imginfo.new_width)-1) - x] = tmp;
            }
    }
    return 0;
}

int buffalloc(pixel ***new_buff, unsigned int height, unsigned int width)
{
    int y;
    if (((*new_buff) = (pixel **) malloc(height * sizeof(pixel *))) == NULL) return -1;

    for (y = 0; y < height; y++)
        if (((*new_buff)[y] = (pixel *) malloc(width * sizeof(pixel))) == NULL) return -1;

    return 0;
}

int mono(ppm_image_handler *handler) // TODO: should return error
{
    int x;
    int y;

    float matrix[] = {0.1250, 1.0000, 0.1875, 0.8125,
                    0.6250, 0.3750, 0.6875, 0.4375,
                    0.2500, 0.8750, 0.0625, 0.9375,
                    0.7500, 0.5000, 0.5625, 0.3125};

    int k = 4;

    handler->imginfo.file_type = FILETYPE_PBM;
    handler->imginfo.new_height = handler->imginfo.height;
    handler->imginfo.new_width = handler->imginfo.width;

    if (buffalloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == -1) return -1;

// bayer 4x4  
// ------------------------------------------------------------------------------------------------------------------------------------------------------
    for (x = 0; x < k*k; x++)
        matrix[x] = matrix[x] * 255;

    for (y = 0; y < handler->imginfo.new_height; y++)
    {
        for (x = 0; x < handler->imginfo.new_width; x++)
        {
            unsigned char oldpixel = (handler->imginfo.buff[y][x].r + handler->imginfo.buff[y][x].g + handler->imginfo.buff[y][x].b) / 3;
            if (oldpixel >= matrix[x%k*k+y%k])
                handler->imginfo.new_buff[y][x].r = 0;
            else
                handler->imginfo.new_buff[y][x].r = 1;
        }
    }
// ------------------------------------------------------------------------------------------------------------------------------------------------------
    return 0;
}

int gray(ppm_image_handler *handler) // TODO: should return error
{
    int x;
    int y;

    handler->imginfo.file_type = FILETYPE_PGM;
    handler->imginfo.new_height = handler->imginfo.height;
    handler->imginfo.new_width = handler->imginfo.width;

    if (buffalloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == -1) return -1;

    for (y = 0; y < handler->imginfo.new_height; y++)
        for (x = 0; x < handler->imginfo.new_width; x++)
        {
            handler->imginfo.new_buff[y][x].r = (unsigned char)((handler->imginfo.buff[y][x].r + handler->imginfo.buff[y][x].g + handler->imginfo.buff[y][x].b) / 3);
        }


    return 0;
}

int doProcessPPM(ppm_image_handler *handler)
{
    int retsz;

    // initialize ppm_image_handler
    handler->filep = fopen(handler->filename, "rb");
    if (handler->filep == NULL)
    {
        printf("error. can not open file: %s\n", handler->filename);
        return -1;
    }

    if (fseek(handler->filep , 0 , SEEK_END) < 0)
    {
        printf("error. can not set file position in fseek.\n");
        return -1;
    }
    
    // get file size and allocate buffer for the whole file
    handler->filesize = ftell(handler->filep);
    rewind(handler->filep);
    handler->file_buffer = (unsigned char*) malloc(sizeof(unsigned char) * handler->filesize);
    if (handler->file_buffer == NULL)
    {
        printf("error. can not allocate memory\n");
        return -1;
    }
    retsz = fread(handler->file_buffer, 1, handler->filesize, handler->filep);
    if (retsz != handler->filesize)
    {
        printf("error in reading input file.\n");
        return -1;
    }

    // initialize variables before parsing
    handler->index_buffer = 0;
    handler->tkn.current_char = '\n';

    // get header file information
    if (getImageInfo(handler) != 0)
    {
        free(handler->file_buffer);
        fclose(handler->filep);
        return -1;
    }
  
	printf("height: %0d\n", handler->imginfo.height);
	printf("width: %0d\n", handler->imginfo.width);
//    // process image
    if (handler->arg_flag.resize_enable)
    {
        init_img_scale_info(handler);

        if ((calc_contributions(&(handler->scale_info))) == -1) return -1;

        imresize(handler);
        release_scale_info(&(handler->scale_info));
    }

    if (handler->arg_flag.rotate_enable)
    {
        if (handler->arg_flag.resize_enable) // TODO: this is ugly
        {
            releaseBuffer(handler);
            handler->imginfo.buff = handler->imginfo.new_buff;
            handler->imginfo.height = handler->imginfo.new_height;
            handler->imginfo.width = handler->imginfo.new_width;
        }
        rotate(handler);
    }

    if (handler->arg_flag.gray_enable)
    {
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable) // TODO: this is ugly
        {
            releaseBuffer(handler);
            handler->imginfo.buff = handler->imginfo.new_buff;
            handler->imginfo.height = handler->imginfo.new_height;
            handler->imginfo.width = handler->imginfo.new_width;
        }
        gray(handler);
    }
        
    if (handler->arg_flag.mono_enable)
    {
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable) // TODO: this is ugly
        {
            releaseBuffer(handler);
            handler->imginfo.buff = handler->imginfo.new_buff;
            handler->imginfo.height = handler->imginfo.new_height;
            handler->imginfo.width = handler->imginfo.new_width;
        }
        mono(handler);
    }
	if (handler->arg_flag.flipv_enable)
    {
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable) // TODO: this is ugly
        {
            releaseBuffer(handler);
            handler->imginfo.buff = handler->imginfo.new_buff;
            handler->imginfo.height = handler->imginfo.new_height;
            handler->imginfo.width = handler->imginfo.new_width;
        }
        flip(handler,1);
    }
        
	if (handler->arg_flag.fliph_enable)
    {
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable) // TODO: this is ugly
        {
            releaseBuffer(handler);
            handler->imginfo.buff = handler->imginfo.new_buff;
            handler->imginfo.height = handler->imginfo.new_height;
            handler->imginfo.width = handler->imginfo.new_width;
        }
        flip(handler,0);
    }
        
    // write the processed image to file
    if (putImageToFile(handler) != 0)
    {
        free(handler->file_buffer);
        fclose(handler->filep);
        return -1;
    }

    // buffer is reused for flipv and fliph
    if (!(handler->arg_flag.flipv_enable || handler->arg_flag.fliph_enable)) releaseBuffer(handler);

    // release allocated image buffer
    free(handler->file_buffer);
    fclose(handler->filep);
    return 0;
}
