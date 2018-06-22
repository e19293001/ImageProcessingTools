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

void calc_contributions(img_scale_info *scale_info);
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
	handler.filename = NULL;

	for (x = 1; x < argc; x++)
	{
		if (argv[x][0] == '-') {
			if (argv[x][1] == 'f') {
				if (argv[x][2] == 'h')
				{
					printf("flip horizontal\n");
				}
				else if (argv[x][2] == 'v')
				{
					printf("flip vertical\n");
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
				printf("handler.rotate_info.angle: %0.0f\n", handler.rotate_info.angle);
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
                printf("gray\n");
            }
			else if (strcmp(&argv[x][1], "mono") == 0)
			{
                handler.arg_flag.mono_enable = 1;
				printf("mono\n");
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
    if (doProcessPPM(&handler) != 0)
    {
        return -1;
    }

    return 0;
}

void printBinary(unsigned char t)
{
    int x;
    for (x = 0; x < 8; x++)
    {
        if ((t << x) & (1 << 7))
        {
            printf("1");
        }
        else
        {
            printf("0");
        }
    }
    printf("\n");
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
    //unsigned int img_sz;


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

    //sprintf(fileout, "%s", "# generated by ppmx_edward\n");
    //error = fwrite(fileout, 1, strlen(fileout), fofp);
    //if (error != strlen(fileout))
    //{
    //    printf("failed in writing to %s\n", fileout);
    //    return -1;
    //}

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
        int a = 0;
        //printBinary(0xAB);
        printf("\n");
        
        //for (y = 0; y < handler->imginfo.new_height; y++)
        //{
        //    z = 0;
        //    tmp = 1;
        //    for (x = 0; x < (handler->imginfo.new_width); x++)
        //    {
        //        printf("%0d", handler->imginfo.new_buff[y][x].r);
        //        z = z | (handler->imginfo.new_buff[y][x].r << tmp);
        //        //printf("[ %0d ] ", z);
        //        //printBinary(z);
        //        if (tmp % 8 == 0)
        //        {
        //            printf(" ");
        //            tmp = 0;
        //        }
        //        tmp++;
        //    }
        //    printf("\n");
        //}

        for (y = 0; y < handler->imginfo.new_height; y++)
        {
            for (x = 0, tmp = 1, z = 0; x < (handler->imginfo.new_width); x++, tmp++)
            {
                //printf("%0d", handler->imginfo.new_buff[y][x].r);
                //printf("[ %0d ] ", z);
                z = z | (handler->imginfo.new_buff[y][x].r << (8 - tmp));
                if (tmp % 8 == 0)
                {
                    //printBinary(z);
                    if ((error = fwrite(&z,1, 1, fofp)) != 1)
                    {
                        printf("failed in writing to %s\n", fileout);
                        return -1;
                    }
                    tmp = 0;
                    z = 0;
                }
                //tmp++;
            }
            //printf("tmp: %0d\n", tmp);
            if (tmp-1 != 0)
            {
                //printBinary(z);
                if ((error = fwrite(&z,1, 1, fofp)) != 1)
                {
                    printf("failed in writing to %s\n", fileout);
                    return -1;
                }
            }
            //printf("\n");
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
    if (handler->imginfo.new_height != 0 || handler->imginfo.new_height != 0)
        free(handler->imginfo.new_buff);

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
    if (handler->tkn.current_char == EOF)
    {
        return;
    }

    if (handler->index_buffer < handler->filesize)
    {
        handler->tkn.current_char = handler->file_buffer[handler->index_buffer++];
    }
    else
    {
        handler->tkn.current_char = EOF;
    }

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
    while (isspace(handler->tkn.current_char))
    {
        getNextChar(handler);
    }

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
    else
    { // return error for anything else
        return -1;
    }

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
            if (getNextPixel(handler, &handler->imginfo.buff[y][x]) != 0)
            {
                return -1;
            }            
        }
    }
    handler->imginfo.new_width = 0;
    handler->imginfo.new_height = 0;

    if (handler->filesize != handler->index_buffer)
    {
        printf("file format error\n");
    }

    return 0;
}

void releaseBuffer(ppm_image_handler *handler)
{
    unsigned int y;
    for (y = 0; y < handler->imginfo.height; y++)
    {
        free(handler->imginfo.buff[y]);
    }
    free(handler->imginfo.buff);
}

float cubic(float x)
{
    float ret = 0;
    float absx = fabs(x);
    float absx2 = absx * absx;
    float absx3 = absx2 * absx;

    if (absx < 1) 
    {
        ret = (1.5*absx3) - (2.5*absx2) + 1;
    }

    if ((1 < absx) && (absx <= 2))
    {
        ret = ret + ((-0.5*absx3) + (2.5*absx2) - (4*absx) + 2);
    }
            
    return ret;
}

int mod(int a, int b)
{
    int r = 0;
    if (b != 0) r = a % b;
    return r < 0 ? r + b : r;
}

void calc_contributions(img_scale_info *scale_info)
{
    int x = 0;
    int y = 0;
    unsigned int aux_size = scale_info->input_size * 2;
    int *aux;
    float **indices;
    float **weights;

    printf("scale_info->output_size: %0d\n", scale_info->output_size);
    indices = (float **) malloc(scale_info->output_size * sizeof(float*));
    weights = (float **) malloc(scale_info->output_size * sizeof(float*));

    if (indices == NULL || weights == NULL) {
        printf("fatal. allocating indices\n");
        return;
    }

    for (y = 0; y < scale_info->output_size; y++)
    {
        indices[y] = (float *) malloc(scale_info->P * sizeof(float));
        weights[y] = (float *) malloc(scale_info->P * sizeof(float));

        if (indices[y] == NULL)
        {
            printf("fatal. allocating indices");
            return;
        }
        if (weights[y] == NULL)
        {
            printf("fatal. allocating weights");
            return;
        }
    }
    aux = (int *) malloc(aux_size * (sizeof(int)));
    if (aux == NULL)
    {
        printf("fatal. allocating aux\n");
        return;
    }            
    memset(aux, 0, aux_size);

    y = 0;
    for (x = 0; x < scale_info->input_size; x++)
    {
        aux[y++] = x;
    }

    for (x = scale_info->input_size-1; x >= 0; x--)
    {
        aux[y++] = x;
    }

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
    {
        for (x = 0; x < scale_info->P; x++)
        {
            float u = ((y+1) / scale_info->scale) + (0.5 * (1 - (1 / scale_info->scale)));
            weights[y][x] = cubic(u - indices[y][x] - 1);
        }
    }

    for (y = 0; y < scale_info->output_size; y++)
    {
        for (x = 0; x < scale_info->P; x++)
        {
            indices[y][x] = aux[mod(((int) indices[y][x]),aux_size)];
        }
    }

    printf("scale_info->P: %0d\n", scale_info->P);
    scale_info->num_non_zero = 0;
    for (x = 0; x < scale_info->P; x++)
    {
        if (weights[0][x] != 0.0f)
        {
            scale_info->num_non_zero = scale_info->num_non_zero + 1;
        }
    }

    printf("nonzero: %0d\n", scale_info->num_non_zero);
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
        {
            if (weights[y][x] != 0.0f)
            {
                scale_info->indices[y][ind_w_ptr_x] = indices[y][x];
                scale_info->weights[y][ind_w_ptr_x] = weights[y][x];
                ind_w_ptr_x = ind_w_ptr_x + 1;
            }
        }
    }

    printf("scale_info->output_size: %0d\n", scale_info->output_size);
    printf("scale_info->num_non_zero: %0d\n", scale_info->num_non_zero);
        
    for (y = 0; y < scale_info->output_size; y++)
    {
        free(indices[y]);
        free(weights[y]);
    }
	
    free(weights);
    free(indices);
    free(aux);
    printf("done calc_contributions\n");
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

	//printf("angle: %0.0f\n", angle);

	calc_rot_size(angle, handler->imginfo.width, handler->imginfo.height, &handler->imginfo.new_width, &handler->imginfo.new_height);
	angle = to_radians(handler->rotate_info.angle);
	
	printf("rotate\n");
    printf("old_height: %0d\n", handler->imginfo.height);
    printf("old_width: %0d\n", handler->imginfo.width);
    printf("new_height: %0d\n", handler->imginfo.new_height);
    printf("new_width: %0d\n", handler->imginfo.new_width);

	x_center_in = floor(handler->imginfo.width / 2);
	y_center_in = floor(handler->imginfo.height / 2);

	x_offset = floor(handler->imginfo.new_width / 2) - floor(handler->imginfo.width / 2);
	y_offset = floor(handler->imginfo.new_height / 2) - floor(handler->imginfo.height / 2);

    handler->imginfo.new_buff = (pixel **) malloc(handler->imginfo.new_height * sizeof(pixel *));
    if (handler->imginfo.new_buff == NULL)
    {
        return -1;
    }

    for (y = 0; y < handler->imginfo.new_height; y++)
    {
        handler->imginfo.new_buff[y] = (pixel *) malloc(handler->imginfo.new_width * sizeof(pixel));
        memset(handler->imginfo.new_buff[y], 0x00, handler->imginfo.new_width * sizeof(pixel));
        if (handler->imginfo.new_buff[y] == NULL)
        {
            return -1;
        }
    }

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

            //nX = round(newX + x_center_in);
            //nY = round(newY + y_center_in);

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
    printf("new_height: %0d\n", handler->imginfo.new_height);
	printf("output_size: %0d\n", handler->scale_info.output_size);
    handler->imginfo.new_width = handler->scale_info.output_size;

    handler->imginfo.new_buff = (pixel **) malloc(handler->imginfo.new_height * sizeof(pixel *));
    if (handler->imginfo.new_buff == NULL)
    {
        return -1;
    }

    for (y = 0; y < handler->imginfo.new_height; y++)
    {
        handler->imginfo.new_buff[y] = (pixel *) malloc(handler->imginfo.new_width * sizeof(pixel));
        if (handler->imginfo.new_buff[y] == NULL)
        {
            return -1;
        }
    }
  
    printf("imresize\n");

    for (y = 0; y < handler->imginfo.height; y++)
    {
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
    }
    
    return 0;
}

int mono(ppm_image_handler *handler) // TODO: should return error
{
    int x;
    int y;

    int matrix[4][4] = {{1, 9, 3, 11} , {13, 5, 15, 7} , {4, 12, 2, 10} , {16, 8, 14, 6}};

    handler->imginfo.file_type = FILETYPE_PBM;
    handler->imginfo.new_height = handler->imginfo.height;
    handler->imginfo.new_width = handler->imginfo.width;
    printf("height: %0d\n", handler->imginfo.new_height);
    printf("width: %0d\n", handler->imginfo.new_width);

    handler->imginfo.new_buff = (pixel **) malloc(handler->imginfo.new_height * sizeof(pixel *));
    if (handler->imginfo.new_buff == NULL)
    {
        return -1;
    }

    for (y = 0; y < handler->imginfo.new_height; y++)
    {
        handler->imginfo.new_buff[y] = (pixel *) malloc(handler->imginfo.new_width * sizeof(pixel));
        if (handler->imginfo.new_buff[y] == NULL) return -1;
    }

    for (y = 0; y < handler->imginfo.new_height; y++)
    {
        for (x = 0; x < handler->imginfo.new_width; x++)
        {
            float mratio = 1.0 / 12;
            float mfactor = 255.0 / 5;

            unsigned char oldpixel = (unsigned char)((handler->imginfo.buff[y][x].r + handler->imginfo.buff[y][x].g + handler->imginfo.buff[y][x].b) / 3);
            unsigned char value = oldpixel + (mratio * matrix[y%4][x%4] * mfactor);
            if (value < 128)
                handler->imginfo.new_buff[y][x].r = 1;
            else
                handler->imginfo.new_buff[y][x].r = 0;
            
/*
                
            //handler->imginfo.buff[y][x].r

            //handler->imginfo.new_buff[y][x].r = (unsigned char) a;
            
            handler->imginfo.new_buff[y][x].r = (unsigned char)((handler->imginfo.buff[y][x].r + handler->imginfo.buff[y][x].g + handler->imginfo.buff[y][x].b) / 3);
            if (handler->imginfo.new_buff[y][x].r < 128)
            {
                handler->imginfo.new_buff[y][x].r = 1;
            }
            else
            {
                handler->imginfo.new_buff[y][x].r = 0;
            }


            unsigned char old_r = handler->imginfo.buff[y][x].r;
            
            unsigned char new_r = round(old_r / 255);
            
            unsigned char quant_error_r;
            
            handler->imginfo.buff[y][x].r = new_r;
            
            quant_error_r = old_r - new_r;
            
            
            // floyd steignberg
            if (y != 0 && y != handler->imginfo.new_height-1 && x != 0 && x != handler->imginfo.new_width-1)
            {
                handler->imginfo.buff[y][x + 1].r = handler->imginfo.buff[y][x + 1].r + quant_error_r * 7 /16;
                handler->imginfo.buff[y][x - 1].r = handler->imginfo.buff[y][x - 1].r + quant_error_r * 3 / 16;
                handler->imginfo.buff[y + 1][x].r = handler->imginfo.buff[y + 1][x].r + quant_error_r * 5 / 16;
                handler->imginfo.buff[y + 1][x + 1].r = handler->imginfo.buff[y + 1][x + 1].r + quant_error_r * 1 / 16;
            }

            //// jarvis
            //if (y >= 2 && y <= handler->imginfo.new_height-3 && x >= 2 && x <= handler->imginfo.new_width-3)
            //{
            //    handler->imginfo.buff[y][x + 1].r = handler->imginfo.buff[y][x + 1].r + quant_error_r * 7 /48;
            //    handler->imginfo.buff[y][x + 2].r = handler->imginfo.buff[y][x + 2].r + quant_error_r * 5 /48;
            //    handler->imginfo.buff[y + 1][x - 2].r = handler->imginfo.buff[y + 1][x - 2].r + quant_error_r * 3 / 48;
            //    handler->imginfo.buff[y + 1][x - 1].r = handler->imginfo.buff[y + 1][x - 1].r + quant_error_r * 5 / 48;
            //    handler->imginfo.buff[y + 1][x    ].r = handler->imginfo.buff[y + 1][x    ].r + quant_error_r * 7 / 48;
            //    handler->imginfo.buff[y + 1][x + 1].r = handler->imginfo.buff[y + 1][x + 1].r + quant_error_r * 5 / 48;
            //    handler->imginfo.buff[y + 1][x + 2].r = handler->imginfo.buff[y + 1][x + 2].r + quant_error_r * 3 / 48;
            //
            //    handler->imginfo.buff[y + 2][x - 2].r = handler->imginfo.buff[y + 2][x - 2].r + quant_error_r * 1 / 48;
            //    handler->imginfo.buff[y + 2][x - 1].r = handler->imginfo.buff[y + 2][x - 1].r + quant_error_r * 3 / 48;
            //    handler->imginfo.buff[y + 2][x    ].r = handler->imginfo.buff[y + 2][x    ].r + quant_error_r * 5 / 48;
            //    handler->imginfo.buff[y + 2][x + 1].r = handler->imginfo.buff[y + 2][x + 1].r + quant_error_r * 3 / 48;
            //    handler->imginfo.buff[y + 2][x + 2].r = handler->imginfo.buff[y + 2][x + 2].r + quant_error_r * 1 / 48;
            //}
            
            //handler->imginfo.new_buff[y][x].r = (unsigned char)((handler->imginfo.buff[y][x].r + handler->imginfo.buff[y][x].g + handler->imginfo.buff[y][x].b) / 3);
            if (handler->imginfo.new_buff[y][x].r < 128)
            {
                handler->imginfo.new_buff[y][x].r = 1;
            }
            else
            {
                handler->imginfo.new_buff[y][x].r = 0;
            }
*/
            //printf("%0d ", handler->imginfo.new_buff[y][x].r);

            //printf("x: %0d y: %0d val: %0d\n", x, y, handler->imginfo.buff[y][x].r);

        }
        //printf("\n");
    }

    return 0;
}

int gray(ppm_image_handler *handler) // TODO: should return error
{
    int x;
    int y;

    handler->imginfo.file_type = FILETYPE_PGM;
    handler->imginfo.new_height = handler->imginfo.height;
    handler->imginfo.new_width = handler->imginfo.width;
    printf("height: %0d\n", handler->imginfo.new_height);
    printf("width: %0d\n", handler->imginfo.new_width);

    handler->imginfo.new_buff = (pixel **) malloc(handler->imginfo.new_height * sizeof(pixel *));
    if (handler->imginfo.new_buff == NULL)
    {
        return -1;
    }

    for (y = 0; y < handler->imginfo.new_height; y++)
    {
        handler->imginfo.new_buff[y] = (pixel *) malloc(handler->imginfo.new_width * sizeof(pixel));
        if (handler->imginfo.new_buff[y] == NULL) return -1;
    }

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
        printf("resizing...");
        init_img_scale_info(handler);

        calc_contributions(&(handler->scale_info));

        imresize(handler);
        release_scale_info(&(handler->scale_info));
    }

    if (handler->arg_flag.rotate_enable)
    {
        printf("rotating...");
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
        
    // write the processed image to file
    if (putImageToFile(handler) != 0)
    {
        free(handler->file_buffer);
        fclose(handler->filep);
        return -1;
    }

    releaseBuffer(handler);

    // release allocated image buffer
    free(handler->file_buffer);
    fclose(handler->filep);
    return 0;
}



/*


            handler->imginfo.buff[y][x].r = (unsigned char)((handler->imginfo.buff[y][x].r + handler->imginfo.buff[y][x].g + handler->imginfo.buff[y][x].b) / 3);
            //float a_ij = (float) handler->imginfo.new_buff[y][x].r;
            //float a_
            unsigned int ury = handler->imginfo.new_height;
            unsigned int urx = handler->imginfo.new_width;
            #define alpha 0.4375
            #define beta 0.1875
            #define gamma 0.3125
            #define delta 0.0625

            float d[65] = {0.000,0.060,0.114,0.162,0.205,0.243,0.276,0.306,0.332,0.355,
                           0.375,0.393,0.408,0.422,0.435,0.446,0.456,0.465,0.474,0.482,
                           0.490,0.498,0.505,0.512,0.520,0.527,0.535,0.543,0.551,0.559,
                           0.568,0.577,0.586,0.596,0.605,0.615,0.625,0.635,0.646,0.656,
                           0.667,0.677,0.688,0.699,0.710,0.720,0.731,0.742,0.753,0.764,
                           0.775,0.787,0.798,0.810,0.822,0.835,0.849,0.863,0.878,0.894,
                           0.912,0.931,0.952,0.975,1.000};
            float dampening = 0.1;

            float err;
            int l;
            int p;
            int levels = 65;
            p = 2;

            if (y == 0 || y > ury) l = 0;
            else
            {
                // find l so that d[l] is close as possible to a[i][j]
                if (handler->imginfo.buff[y][x].r < 0) l = 0;
                else if (handler->imginfo.buff[y][x].r >= 1) l = 64;
                else
                {
                    int lo_l = 0;
                    int hi_l = 64;
                    while (hi_l - lo_l > p)
                    {
                        int mid_l = (lo_l + hi_l) >> 1;
                        if (handler->imginfo.buff[y][x].r >= d[mid_l]) lo_l = mid_l;
                        else hi_l = mid_l;

                        printf("hi_l: %0d lo_l: %0d p: %0d\n", hi_l, lo_l, p);
                    }
                    if (handler->imginfo.buff[y][x].r - d[lo_l] <= d[hi_l] - handler->imginfo.buff[y][x].r) l = lo_l;
                    else l = hi_l;
                }                            
            }
            err = handler->imginfo.buff[y][x].r - d[l];
            handler->imginfo.buff[y][x].r =  l / p;
            if (y < ury - 1) handler->imginfo.buff[y+1][x].r += alpha * dampening * err;
            if (x < urx - 1)
            {
                if (y > 0)
                {
                    handler->imginfo.buff[y-1][x+1].r += beta * dampening * err;
                }
                handler->imginfo.buff[y][x+1].r += gamma * dampening * err;
                if (y < ury - 1)
                {
                    handler->imginfo.buff[y+1][x+1].r += delta * dampening * err;
                }
            }

            printf("x: %0d y: %0d %0d p: %0d l: %0d\n", x, y, handler->imginfo.buff[y][x].r, p, l);
            handler->imginfo.new_buff[y][x].r = handler->imginfo.buff[y][x].r;
            

*/
