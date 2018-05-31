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
#define TEST_ROTATE

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
    double angle;
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
    pixel **buff;
    pixel **new_buff;
} img_info;

typedef struct args_flag {
    char resize_enable;
    char rotate_enable;
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
int rotateGrayScaleImage(ppm_image_handler *handler);
void releaseBuffer(ppm_image_handler *handler);

#ifdef TEST_RESIZE
int main(int argc, char **argv)
{
    ppm_image_handler handler;

    handler.filename = argv[1];
    handler.arg_flag.resize_enable = 1;

    if (doProcessPPM(&handler) != 0)
    {
        return -1;
    }

    return 0;
}
#endif
#ifdef TEST_ROTATE
int main(int argc, char **argv)
{
    ppm_image_handler handler;

    handler.filename = argv[1];
    handler.arg_flag.rotate_enable = 1;
    handler.arg_flag.resize_enable = 0;

    if (doProcessPPM(&handler) != 0)
    {
        return -1;
    }

    return 0;
}
#else
int main(int argc, char **argv) 
{
    ppm_image_handler handler;
    if (argc != 2)
    {
        printf("invalid number of arguments: %0d\n", argc);
        printf("ppm_edward <file name>\n");
        return -1;
    }
    handler.filename = argv[1];
    if (doProcessPPM(&handler) != 0)
    {
        return -1;
    }
    return 0;
}
#endif

// rotate and convert image to gray scale
int rotateGrayScaleImage(ppm_image_handler *handler)
{
    unsigned int x = 0;
    unsigned int y = 0;

    handler->imginfo.new_height = handler->imginfo.width;
    handler->imginfo.new_width = handler->imginfo.height;

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

    for (y = 0; y < handler->imginfo.height; y++)
    {
        for (x = 0; x < handler->imginfo.width; x++)
        {
            unsigned int new_x;
            unsigned int new_y;
            unsigned char new_color;

            new_x = handler->imginfo.new_width - y - 1;
            new_y = x;

            new_color = (handler->imginfo.buff[y][x].r + handler->imginfo.buff[y][x].b + handler->imginfo.buff[y][x].g) / 3;
            handler->imginfo.new_buff[new_y][new_x].r = new_color;
            handler->imginfo.new_buff[new_y][new_x].g = new_color;
            handler->imginfo.new_buff[new_y][new_x].b = new_color;
        }
    }
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
    //unsigned int img_sz;


    memset(fileout, '\0', MAX_HEADER_CHARS);
    strncpy(fileout, handler->filename, fileout_size);
    strcat(fileout, ".out");

    //img_sz = handler->imginfo.new_width * handler->imginfo.new_height;

    if ((fofp = fopen(fileout, "wb")) == NULL)
    {
        printf("unable to open file for writing: %s\n", fileout);
        return -1;
    }

    error = fwrite("P6\n", 1, 3, fofp);
    if (error != 3)
    {
        printf("Not a P6 format\n");
        return -1;
    }

    sprintf(fileout, "%s", "# generated by ppm_edward\n");
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

    sprintf(fileout, "%u\n", handler->imginfo.max_color);
    error = fwrite(fileout, 1, strlen(fileout), fofp);
    if (error != strlen(fileout))
    {
        printf("failed in writing to %s\n", fileout);
        return -1;
    }

    //printf("new_height: %0d\n", handler->imginfo.new_height);
    //printf("new_width: %0d\n", handler->imginfo.new_width);
    
    for (y = 0; y < handler->imginfo.new_height; y++)
    {
        for (x = 0; x < handler->imginfo.new_width; x++)
        {
            error = fwrite(&handler->imginfo.new_buff[y][x].r,1, 1, fofp);
            if (error != 1)
            {
                printf("failed in writing to %s\n", fileout);
                return -1;
            }
            error = fwrite(&handler->imginfo.new_buff[y][x].g,1, 1, fofp);
            if (error != 1)
            {
                printf("failed in writing to %s\n", fileout);
                return -1;
            }
            error = fwrite(&handler->imginfo.new_buff[y][x].b,1, 1, fofp);
            if (error != 1)
            {
                printf("failed in writing to %s\n", fileout);
                return -1;
            }
        }
    }

    for (y = 0; y < handler->imginfo.new_height; y++)
    {
        free(handler->imginfo.new_buff[y]);
    }

    fclose(fofp);
    if (handler->imginfo.new_height != 0 || handler->imginfo.new_height != 0)
        free(handler->imginfo.new_buff);

    return 0;
}

int getNextPixel(ppm_image_handler *handler, pixel *pix)
{
    pixel ret;

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

    for (x = 0; x < aux_size; x++)
    {
        printf("aux[%0d]: %0d\n", x, aux[x]);
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

    for (y = 0; y < scale_info->output_size; y++)
    {
        for (x = 0; x < scale_info->num_non_zero; x++)
        {
            printf("scale_info->weights[%0d][%0d] = %0f\n", y, x, scale_info->weights[y][x]);
        }
    }

    for (y = 0; y < scale_info->output_size; y++)
    {
        for (x = 0; x < scale_info->num_non_zero; x++)
        {
            printf("scale_info->indices[%0d][%0d] = %0d\n", y, x, scale_info->indices[y][x]);
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

void init_img_rotate_info(ppm_image_handler *handler)
{
    handler->rotate_info.angle = 10.5;
}

void init_img_scale_info(ppm_image_handler *handler)
{
    handler->scale_info.kernel_width = 4.0;
    handler->scale_info.P = handler->scale_info.kernel_width + 2;
    handler->scale_info.input_size = handler->imginfo.width;
    handler->imginfo.new_height = handler->imginfo.height;
    //printf("handler->imginfo.height: %0d\n", handler->imginfo.height);
    //scale_info->output_size = 10;
    //scale_info->output_size = 512;
    handler->scale_info.output_size = handler->imginfo.width / 2; // need to set from command line argument
    handler->scale_info.scale = getScaleFromSize(handler->scale_info.input_size, handler->scale_info.output_size);
}

double to_radians(double degrees) {
	//return (degrees) * (M_PI/180.0);
	return (degrees * M_PI)/180.0;
}

float to_degrees(float radians) {
    return (radians * 180.0) / M_PI;
}

void calc_rot_size(double angle,
				   unsigned int old_width, unsigned int old_height,
				   unsigned int *new_width, unsigned int *new_height)
{
    double theta1;

    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
	theta1 = to_radians(angle);
	d = old_height;
	c = old_width;

	b = c * sin(theta1);
	a = c * cos(theta1);
	f = d * sin(theta1);
	e = d * cos(theta1);
	*new_width = floor(a + f);
	*new_height = floor(b + e);
}	

int rotate(ppm_image_handler *handler)
{
    int x;
    int y;
    int x_center_in;
    int y_center_in;

    double angle = to_radians(handler->rotate_info.angle);

	int x_offset;
	int y_offset;

	calc_rot_size(handler->rotate_info.angle <= 90 ? handler->rotate_info.angle : handler->rotate_info.angle-90,
				  handler->imginfo.width, handler->imginfo.height,
				  &handler->imginfo.new_width, &handler->imginfo.new_height);
	
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
        memset(handler->imginfo.new_buff[y], 0xff, handler->imginfo.new_width * sizeof(pixel));
        if (handler->imginfo.new_buff[y] == NULL)
        {
            return -1;
        }
    }

	for (y = 0; y < handler->imginfo.new_height; y++)
    {
        for (x = 0; x < handler->imginfo.new_width; x++)
        {
            double newX;
            double newY;
			int xx;
			int yy;

            int x0;
            int y0;

            int nX;
            int nY;

			xx = x - x_offset;
			yy = y - y_offset;
			x0 = xx - x_center_in;
			y0 = yy - y_center_in;

            newX = (cos(angle) * (double) (x0)) + (sin(angle) * (double) (y0));
            newY = -(sin(angle) * (double)(x0)) + (cos(angle) * (double) (y0));

            nX = floor(newX + x_center_in);
            nY = floor(newY + y_center_in);

			if ((nX < handler->imginfo.width) && (nY < handler->imginfo.height) && (nY >= 0) && (nX >= 0))
            {
				handler->imginfo.new_buff[yy+y_offset][xx+x_offset] = handler->imginfo.buff[nY][nX];
            }
        }
    }

	printf("done\n");

    //handler->imginfo.new_buff[0][60].r = 0xFF;
    //handler->imginfo.new_buff[0][60].g = 0x00;
    //handler->imginfo.new_buff[0][60].b = 0x00;
    
    return 0;
}
        
int imresize(ppm_image_handler *handler)
{
    int x;
    int y;

    handler->imginfo.new_height = handler->imginfo.height;
    printf("new_height: %0d\n", handler->imginfo.new_height);
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
  
//    // process the image
//    if (rotateGrayScaleImage(handler) != 0)
//    {
//        return -1;
//    }
    if (handler->arg_flag.resize_enable)
    {
        init_img_scale_info(handler);

        calc_contributions(&(handler->scale_info));

        imresize(handler);
        release_scale_info(&(handler->scale_info));
    }

    if (handler->arg_flag.rotate_enable)
    {
        init_img_rotate_info(handler);
        rotate(handler);
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
