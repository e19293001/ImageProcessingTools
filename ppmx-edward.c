// Edward Sillador

// * 06/16/2018 - added flip (vertical / horizontal), scale, rotation, 

// * 12/2017 - initial version

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#define M_PI 3.14159265358979323846
#define DATA_BUFLEN 10
#define MAX_HEADER_CHARS 128
#define PPM_ERROR -1
#define PPM_NOERROR 0
#define PPM_UNSIGNED 1
#define PPM_MAGIC_NUM 2

#define FILETYPE_PPM 0
#define FILETYPE_PGM 1
#define FILETYPE_PBM 2

// visual studio 2010 does not include round() in math.h
#define round(val) floor(val + 0.5)

#define CHECK_ERROR(condition, message) \
    if (condition) \
    { \
        printf(message); \
        return PPM_ERROR; \
    } 

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
    FILE *filep;
    unsigned char* file_buffer;
    char *filename;
    unsigned int filesize;
    unsigned int index_buffer;
    token tkn;
    unsigned int output_width_size;
    double angle;
    char norotate;
} ppm_image_handler;

int calc_contributions(int in_size, int out_size, double scale, double k_width, double ***out_weights, int ***out_indices, int *contrib_size);
int mod(int a, int b);
double cubic(double x);
int getNextToken(ppm_image_handler *handler, token *current_token);
void getNextChar(ppm_image_handler *handler);
int getNextPixel(ppm_image_handler *handler, pixel *pix);
int doProcessPPM(ppm_image_handler *handler);
int getImageInfo(ppm_image_handler *handler);
void releaseBuffer(pixel ***new_buff, unsigned int height);
int image_buff_alloc(pixel ***new_buff, unsigned int height, unsigned int width);
void calc_rot_size(double angle,
                   unsigned int old_width, unsigned int old_height,
                   unsigned int *new_width, unsigned int *new_height);
int putImageToFile(ppm_image_handler *handler);
void usage();
void renewBuffer(ppm_image_handler *handler);

int main(int argc, char *argv[])
{
    ppm_image_handler handler;
    int x;
    int filename_flag = 0;

    memset(&handler, 0, sizeof(ppm_image_handler));

    for (x = 1; x < argc; x++)
        if (argv[x][0] == '-')
            if (argv[x][1] == 'f')
                if (argv[x][2] == 'h') 
                {
                    CHECK_ERROR(handler.arg_flag.fliph_enable, "Error: Duplicate options not allowed\n")
                    CHECK_ERROR(handler.arg_flag.flipv_enable, "Error: Conflicting options not allowed\n")
                    handler.arg_flag.fliph_enable = 1;
                }
                else if (argv[x][2] == 'v') 
                {
                    CHECK_ERROR(handler.arg_flag.flipv_enable, "Error: Duplicate options not allowed\n")
                    CHECK_ERROR(handler.arg_flag.fliph_enable, "Error: Conflicting options not allowed\n")
                    handler.arg_flag.flipv_enable = 1;
                }
                else
                {
                    printf("Error: invalid option for flip.\nallowed options are -fh -fv only.\n");
                    return PPM_ERROR;
                }
            else if (argv[x][1] == 'w')
            {
                int index = 2;
                while (argv[x][index] != 0)    CHECK_ERROR(isalpha(argv[x][index++]), "Error: invalid option for scaling.\n")
                CHECK_ERROR(handler.arg_flag.resize_enable, "Error: Duplicate options not allowed\n")
                handler.arg_flag.resize_enable = 1;
                handler.output_width_size = (int) atoi(&argv[x][2]);
            }
            else if (argv[x][1] == 'r')
            {
                int index = 2;
                CHECK_ERROR((argv[x][2] == 0), "Error: invalid option for rotate\n")
                CHECK_ERROR(handler.arg_flag.rotate_enable, "Error: Duplicate options not allowed\n")
                handler.arg_flag.rotate_enable = 1;
                while (argv[x][index] != 0)    CHECK_ERROR(isalpha(argv[x][index++]), "Error: invalid option for rotate.\n")
                handler.angle = (double) atoi(&argv[x][2]);
                CHECK_ERROR(handler.angle < 0 || handler.angle >= 360, "Error: invalid option for rotate.\n")
            }
            else if (strcmp(&argv[x][1], "gray") == 0) 
            {
                CHECK_ERROR(handler.arg_flag.gray_enable, "Error: Duplicate options not allowed\n")
                CHECK_ERROR(handler.arg_flag.mono_enable, "Error: Conflicting options not allowed\n")
                handler.arg_flag.gray_enable = 1;
            }
            else if (strcmp(&argv[x][1], "mono") == 0) 
            {
                CHECK_ERROR(handler.arg_flag.mono_enable, "Error: Duplicate options not allowed\n")
                CHECK_ERROR(handler.arg_flag.gray_enable, "Error: Conflicting options not allowed\n")
                handler.arg_flag.mono_enable = 1;
            }
            else
            {
                printf("Error: invalid option: %s\n", &argv[x][1]);
                usage();
                return PPM_ERROR;
            }
        else 
        {
            CHECK_ERROR(filename_flag, "Error: invalid options\n");
            handler.filename = &argv[x][0];
            filename_flag = 1;
        }
    if (handler.filename == NULL || filename_flag == 0)
    {
        usage();
        return PPM_ERROR;
    }
    if (doProcessPPM(&handler) != 0) return PPM_ERROR;

    return 0;
}

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

// write output image to file
int putImageToFile(ppm_image_handler *handler)
{
    int fileout_size =  strlen(handler->filename) + 5;
    char fileout[MAX_HEADER_CHARS];
    FILE *fofp;
    unsigned int x;
    unsigned int y;

    memset(fileout, '\0', MAX_HEADER_CHARS);
    strncpy(fileout, handler->filename, fileout_size);
    strcat(fileout, ".out");

    CHECK_ERROR(handler->imginfo.new_buff == NULL, "Error: no data to write\n")

    CHECK_ERROR((fofp = fopen(fileout, "wb")) == NULL, "Error: unable to open file for writing\n")

    if (handler->imginfo.file_type == FILETYPE_PGM)
    {
        CHECK_ERROR((fwrite("P5\n", 1, 3, fofp)) != 3, "Error: failed in writing to file\n")
    }
    else if (handler->imginfo.file_type == FILETYPE_PBM)
    {
        CHECK_ERROR((fwrite("P4\n", 1, 3, fofp)) != 3, "Error: failed in writing to file\n")
    }
    else
    {
        CHECK_ERROR((fwrite("P6\n", 1, 3, fofp)) != 3, "Error: failed in writing to file\n")
    }

    sprintf(fileout, "%s", "# generated by ppmx_edward\n");
    CHECK_ERROR(((fwrite(fileout, 1, strlen(fileout), fofp)) != strlen(fileout)), "Error: failed in writing to file\n")

    sprintf(fileout, "%u ", handler->imginfo.new_width);
    CHECK_ERROR(((fwrite(fileout, 1, strlen(fileout), fofp)) != strlen(fileout)), "Error: failed in writing to file\n")

    sprintf(fileout, "%u\n", handler->imginfo.new_height);
    CHECK_ERROR(((fwrite(fileout, 1, strlen(fileout), fofp)) != strlen(fileout)), "Error: failed in writing to file\n")

    if (!(handler->imginfo.file_type == FILETYPE_PBM))
    {
        sprintf(fileout, "%u\n", handler->imginfo.max_color);
        CHECK_ERROR(((fwrite(fileout, 1, strlen(fileout), fofp)) != strlen(fileout)), "Error: failed in writing to file\n")
    }

    if (handler->imginfo.file_type == FILETYPE_PGM)
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                CHECK_ERROR(((fwrite(&handler->imginfo.new_buff[y][x].r,1, 1, fofp)) != 1), "Error: failed in writing to file\n")
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
                    CHECK_ERROR(((fwrite(&z,1, 1, fofp)) != 1), "Error: failed in writing to file\n")
                    tmp = 0;
                    z = 0;
                }
            }
            if (tmp-1 != 0)
            {
                CHECK_ERROR(((fwrite(&z,1, 1, fofp)) != 1), "Error: failed in writing to file\n")
            }
        }
    }
    else
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                CHECK_ERROR(((fwrite(&handler->imginfo.new_buff[y][x].r,1, 1, fofp)) != 1), "Error: failed in writing to file\n")
                CHECK_ERROR(((fwrite(&handler->imginfo.new_buff[y][x].g,1, 1, fofp)) != 1), "Error: failed in writing to file\n")
                CHECK_ERROR(((fwrite(&handler->imginfo.new_buff[y][x].b,1, 1, fofp)) != 1), "Error: failed in writing to file\n")
            }

    for (y = 0; y < handler->imginfo.new_height; y++)
        free(handler->imginfo.new_buff[y]);

    fclose(fofp);

    if (handler->imginfo.new_width != 0 || handler->imginfo.new_height != 0) free(handler->imginfo.new_buff);

    return PPM_NOERROR;
}

int getNextPixel(ppm_image_handler *handler, pixel *pix)
{
    CHECK_ERROR((handler->index_buffer > handler->filesize), "Error: unexpected end of file.\n")
    (*pix).r = handler->file_buffer[handler->index_buffer++];
    (*pix).g = handler->file_buffer[handler->index_buffer++];
    (*pix).b = handler->file_buffer[handler->index_buffer++];

    return PPM_NOERROR;
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
    int index = 0;
    // returns error for any invalid token
    handler->tkn.kind = PPM_ERROR;

    // ignore spaces
    while (isspace(handler->tkn.current_char)) getNextChar(handler);

    // if token is a digit
    if (isdigit(handler->tkn.current_char))
    {
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
        do {
            handler->tkn.data[index++] = handler->tkn.current_char;
            getNextChar(handler);
        } while (isalnum(handler->tkn.current_char));

        handler->tkn.data[index] = '\0';

        if ((strncmp(handler->tkn.data, "P6", DATA_BUFLEN)) == 0) handler->tkn.kind = PPM_MAGIC_NUM;

        getNextChar(handler);
    }
    else return PPM_ERROR; // return error for anything else

    *current_token = handler->tkn;
    return PPM_NOERROR;
}

// converts the buffer to 2 dimensional image
int getImageInfo(ppm_image_handler *handler)
{
    token current_token;
    unsigned int x;
    unsigned int y;

    // retrieve the magic number
    CHECK_ERROR((getNextToken(handler, &current_token) != PPM_NOERROR), "error in getting next token. wrong format.\n")
    CHECK_ERROR((current_token.kind != PPM_MAGIC_NUM), "error. invalid file format.\n")
    handler->imginfo.file_type = FILETYPE_PPM;

    // retrieve the width
    CHECK_ERROR((getNextToken(handler, &current_token) != PPM_NOERROR), "error in getting next token. wrong format.\n")

    if (current_token.kind != PPM_UNSIGNED)
    {
        printf("error. invalid file format. unable to parse width from input file.\n");
        return PPM_ERROR;
    }
    handler->imginfo.width = atoi(current_token.data);

    // retrieve the height
    CHECK_ERROR((getNextToken(handler, &current_token) != PPM_NOERROR), "error in getting next token. wrong format.\n")

    CHECK_ERROR((current_token.kind != PPM_UNSIGNED), "error. invalid file format. unable to parse height from input file.\n")
    handler->imginfo.height = atoi(current_token.data);

    // retrieve the maximum color
    CHECK_ERROR((getNextToken(handler, &current_token) != PPM_NOERROR), "error in getting next token. wrong format.\n")
    CHECK_ERROR((current_token.kind != PPM_UNSIGNED), "error. invalid file format. unable to parse maximum color from input file.\n")
    handler->imginfo.max_color = atoi(current_token.data);
  
    handler->imginfo.buff = (pixel **) malloc(handler->imginfo.height * sizeof(pixel *));
    CHECK_ERROR((handler->imginfo.buff == NULL), "error. can not allocate memory\n")
  
    handler->imginfo.size = handler->imginfo.height * handler->imginfo.width;
    for (y = 0; y < handler->imginfo.height; y++)
    {
        handler->imginfo.buff[y] = (pixel *) malloc(handler->imginfo.width * sizeof(pixel));
        CHECK_ERROR((handler->imginfo.buff[y] == NULL), "error. can not allocate memory\n")
        for (x = 0; x < handler->imginfo.width; x++)
            if (getNextPixel(handler, &handler->imginfo.buff[y][x]) != 0) return PPM_ERROR;
    }
    handler->imginfo.new_width = 0;
    handler->imginfo.new_height = 0;

    CHECK_ERROR((handler->filesize != handler->index_buffer), "file format error\n")

    return PPM_NOERROR;
}

void releaseBuffer(pixel ***new_buff, unsigned int height)
{
    unsigned int y;
    for (y = 0; y < height; y++)
        free((*new_buff)[y]);

    free(*new_buff);
}

double cubic(double x)
{
    double ret = 0;
    double absx = fabs(x);
    double absx2 = absx * absx;
    double absx3 = absx2 * absx;

    if (absx <= 1) ret = (1.5*absx3) - (2.5*absx2) + 1;

    if ((1 < absx) && (absx <= 2)) ret = ret + ((-0.5*absx3) + (2.5*absx2) - (4*absx) + 2);
            
    return ret;
}

int mod(int a, int b)
{
    int r = 0;
    if (b != 0) r = a % b;
    return r < 0 ? r + b : r;
}

int calc_contributions(int in_size, int out_size, double scale, double k_width, double ***out_weights, int ***out_indices, int *contrib_size)
{
    int x = 0;
    int y = 0;
    unsigned int aux_size = in_size * 2;
    int *aux;
    int **indices;
    double **weights;
    int P = 0;
    int num_non_zero = 0;
    int ind_w_ptr_x = 0;
    unsigned char *ind2store;

    if (scale < 1.0) k_width = k_width / scale;

    P = (int) ceil(k_width) + 2;

    CHECK_ERROR(((indices = (int **) malloc(out_size * sizeof(int*))) == NULL), "error. allocating indices\n")
    CHECK_ERROR(((weights = (double **) malloc(out_size * sizeof(double*))) == NULL), "error. allocating indices\n")

    for (y = 0; y < out_size; y++)
    {
        indices[y] = (int *) malloc(P * sizeof(double));
        weights[y] = (double *) malloc(P * sizeof(double));

        CHECK_ERROR((indices[y] == NULL || weights[y] == NULL), "error. allocating memory for weights and indices\n")
    }

    CHECK_ERROR(((aux = (int *) malloc(aux_size * (sizeof(int)))) == NULL), "fatal. allocating memory for aux\n")  

    memset(aux, 0, aux_size);

    for (x = 0, y = 0; x < in_size; x++)
        aux[y++] = x;

    for (x = in_size-1; x >= 0; x--)
        aux[y++] = x;

    // generate indices
    for (y = 0; y < out_size; y++)
        for (x = 0; x < P; x++)
        {
            // generate an array from 1 to output_size
            // divide each element to scale
            double u = ((y+1) / scale) + (0.5 * (1 - (1 / scale)));
            indices[y][x] = floor(u - (k_width / 2)) + (x - 1);
        }

    // generate weights
    if (scale < 1.0)
        for (y = 0; y < out_size; y++)
            for (x = 0; x < P; x++)
            {
                double u = ((y+1) / scale) + (0.5 * (1 - (1 / scale)));
                double t = cubic((u - (double)indices[y][x] - 1) * scale);
                weights[y][x] = scale * t;
            }
    else
        for (y = 0; y < out_size; y++)
            for (x = 0; x < P; x++)
            {
                double u = ((y+1) / scale) + (0.5 * (1 - (1 / scale)));
                weights[y][x] = cubic(u - (double)indices[y][x] - 1);
            }

    for (y = 0; y < out_size; y++)
    {
        double sum = 0.0;
        for (x = 0; x < P; x++) sum += weights[y][x];
        for (x = 0; x < P; x++) weights[y][x] /= sum;
    }

    for (y = 0; y < out_size; y++)
        for (x = 0; x < P; x++)
            indices[y][x] = aux[mod(((int) indices[y][x]),aux_size)];

    for (y = 0; y < out_size; y++)
    {
        int num_non_zero_t = 0;
        for (x = 0; x < P; x++)
            if (weights[y][x] != 0.0f)
            {
                num_non_zero_t = num_non_zero_t + 1;
            }
        if (num_non_zero < num_non_zero_t) num_non_zero = num_non_zero_t;
    }

    CHECK_ERROR(((ind2store = (unsigned char*) malloc(P * sizeof(unsigned char*))) == NULL), "error: allocating ind2store\n")

    for (x = 0; x < P; x++) ind2store[x] = 0;

    for (y = 0; y < out_size; y++)
        for (x = 0; x < P; x++)
            if (weights[y][x] != 0.0f) ind2store[x] = 1;

    CHECK_ERROR((((*out_indices) = (int **) malloc(out_size * sizeof(int*))) == NULL), "error: allocating out_indices\n")
    CHECK_ERROR((((*out_weights) = (double **) malloc(out_size * sizeof(double*))) == NULL), "error: allocating out_weights\n")
    
    for (y = 0; y < out_size; y++)
    {
        CHECK_ERROR((((*out_indices)[y] = (int *) malloc(num_non_zero * sizeof(int))) == NULL), "error: allocating out_indices\n")
        CHECK_ERROR((((*out_weights)[y] = (double *) malloc(num_non_zero * sizeof(double))) == NULL), "error: allocating out_weights\n")
    }

    for (y = 0; y < out_size; y++)
    {
        ind_w_ptr_x = 0;
        for (x = 0; x < P; x++)
        {
            if (ind2store[x])
            {
                (*out_indices)[y][ind_w_ptr_x] = indices[y][x];
                (*out_weights)[y][ind_w_ptr_x] = weights[y][x];
                ind_w_ptr_x = ind_w_ptr_x + 1;
            }
        }
    }

    *contrib_size = num_non_zero;

    for (y = 0; y < out_size; y++)
    {
        free(indices[y]);
        free(weights[y]);
    }
    
    free(ind2store);
    free(weights);
    free(indices);
    free(aux);

    return PPM_NOERROR;
}

void calc_rot_size(double angle,
                   unsigned int old_width, unsigned int old_height,
                   unsigned int *new_width, unsigned int *new_height)
{
    double theta1 = (angle * M_PI)/180.0; // convert to radians
    *new_width = round((old_width * cos(theta1)) + (old_height * sin(theta1)));
    *new_height = round((old_width * sin(theta1)) + (old_height * cos(theta1)));
}    

int rotate(ppm_image_handler *handler)
{
    int x;
    int y;
    int x_center_in;
    int y_center_in;

    double angle;

    int x_offset;
    int y_offset;

    angle = handler->angle;

    if (angle >= 270) angle = 360 - angle;
    else if (angle > 180) angle = angle - 180;
    else if (angle > 90) angle = 180 - angle;

    calc_rot_size(angle, handler->imginfo.width, handler->imginfo.height, &handler->imginfo.new_width, &handler->imginfo.new_height);
    angle = (handler->angle * M_PI) / 180.0; // convert to radians
    
    x_center_in = floor(handler->imginfo.width / 2);
    y_center_in = floor(handler->imginfo.height / 2);

    x_offset = floor(handler->imginfo.new_width / 2) - floor(handler->imginfo.width / 2);
    y_offset = floor(handler->imginfo.new_height / 2) - floor(handler->imginfo.height / 2);

    // 0 degree rotation: no rotation, just copy the buffer
    if (handler->angle == 0)
    {
        handler->norotate = 1;
        handler->imginfo.new_buff = handler->imginfo.buff;
        return PPM_NOERROR;
    }
    else if (image_buff_alloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == PPM_ERROR) return PPM_ERROR;

    // The following are the reasons  orthogonal rotations (90,
    // 180, 270) don't need to use the rotation formula:
    // 1. Prevent round-off errors
    // 2. Faster rotation
    // 3. No need for interpolation

    if (handler->angle == 90) // rotate 90 degrees 
        for (y = 0; y < handler->imginfo.height; y++)
            for (x = 0; x < handler->imginfo.width; x++)
                handler->imginfo.new_buff[x][handler->imginfo.new_width - y - 1] = handler->imginfo.buff[y][x];
    else if (handler->angle == 180)     // rotate 180 degrees
        for (y = 0; y < handler->imginfo.height; y++)
            for (x = 0; x < handler->imginfo.width; x++)
                handler->imginfo.new_buff[handler->imginfo.new_height - y - 1][handler->imginfo.new_width - x - 1] = handler->imginfo.buff[y][x];
    else if (handler->angle == 270) // rotate 270 degrees
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
                handler->imginfo.new_buff[handler->imginfo.new_height - y - 1][x] = handler->imginfo.buff[x][y];
    else
    {
        for (y = 0; y < handler->imginfo.new_height; y++) memset(handler->imginfo.new_buff[y], 0x00, handler->imginfo.new_width * sizeof(pixel));

        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                int xx = x - x_offset;
                int yy = y - y_offset;

                int x0 = xx - x_center_in;
                int y0 = yy - y_center_in;

                // rotation formula
                double nX = ((cos(angle) * (double) (x0)) + (sin(angle) * (double) (y0)) + x_center_in);
                double nY = (-(sin(angle) * (double)(x0)) + (cos(angle) * (double) (y0)) + y_center_in);

                if ((round(nX) < handler->imginfo.width) && (round(nY) < handler->imginfo.height) && (round(nY) >= 0) && (round(nX) >= 0))
                {
                    double q_r = 0.0f;
                    double q_g = 0.0f;
                    double q_b = 0.0f;
                    int j;
                    int i;

                    // no interpolation on the edges
                    if (round(nX) > 1 && round(nY) > 1 && round(nX) < handler->imginfo.width - 2 && round(nY) < handler->imginfo.height - 2)
                    {
                        for (j = 0; j < 4; j++) 
                        {
                            int v = floor(nY) - 1 + j;
                            double p_r = 0.0f;
                            double p_g = 0.0f;
                            double p_b = 0.0f;
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
                    else handler->imginfo.new_buff[yy+y_offset][xx+x_offset] = handler->imginfo.buff[(int)round(nY)][(int)round(nX)];
                }
            }
    }

    return PPM_NOERROR;
}
        
int imresize(ppm_image_handler *handler, int out_size, int dim, double **weights, int **indices, int weights_sz)
{
    int x;
    int y;
    int z;
    
    if (dim == 0) // scale height
    {
        handler->imginfo.new_height = out_size;
        handler->imginfo.new_width = handler->imginfo.width;

        if (image_buff_alloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.width) == PPM_ERROR) return PPM_ERROR;
  
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                double sum_r = 0.0f;
                double sum_g = 0.0f;
                double sum_b = 0.0f;

                for (z = 0; z < weights_sz; z++)
                {
                    sum_r = sum_r + (handler->imginfo.buff[indices[y][z]][x].r * weights[y][z]);
                    sum_g = sum_g + (handler->imginfo.buff[indices[y][z]][x].g * weights[y][z]);
                    sum_b = sum_b + (handler->imginfo.buff[indices[y][z]][x].b * weights[y][z]);
                }
                sum_r = round(sum_r);
                sum_g = round(sum_g);
                sum_b = round(sum_b);

                handler->imginfo.new_buff[y][x].r = (sum_r < 0.0f) ? 0.0f : (sum_r >= 256) ? 255.0f : (int) sum_r;
                handler->imginfo.new_buff[y][x].g = (sum_g < 0.0f) ? 0.0f : (sum_g >= 256) ? 255.0f : (int) sum_g;
                handler->imginfo.new_buff[y][x].b = (sum_b < 0.0f) ? 0.0f : (sum_b >= 256) ? 255.0f : (int) sum_b;
            }
    }
    else // scale width
    {
        handler->imginfo.new_height = handler->imginfo.height;
        handler->imginfo.new_width = out_size;

        if (image_buff_alloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == PPM_ERROR) return PPM_ERROR;
  
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                double sum_r = 0.0f;
                double sum_g = 0.0f;
                double sum_b = 0.0f;

                for (z = 0; z < weights_sz; z++)
                {
                    sum_r = sum_r + handler->imginfo.buff[y][indices[x][z]].r * weights[x][z];
                    sum_g = sum_g + handler->imginfo.buff[y][indices[x][z]].g * weights[x][z];
                    sum_b = sum_b + handler->imginfo.buff[y][indices[x][z]].b * weights[x][z];
                }

                sum_r = round(sum_r);
                sum_g = round(sum_g);
                sum_b = round(sum_b);

                handler->imginfo.new_buff[y][x].r = (sum_r < 0.0f) ? 0.0f : (sum_r >= 256) ? 255.0f : (int) sum_r;
                handler->imginfo.new_buff[y][x].g = (sum_g < 0.0f) ? 0.0f : (sum_g >= 256) ? 255.0f : (int) sum_g;
                handler->imginfo.new_buff[y][x].b = (sum_b < 0.0f) ? 0.0f : (sum_b >= 256) ? 255.0f : (int) sum_b;
            }
    }
    
    return PPM_NOERROR;
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

    if (flip_direction) // flip vertical
        for (y = 0; y < handler->imginfo.new_height/2; y++)
            for (x = 0; x < handler->imginfo.new_width; x++)
            {
                pixel tmp = handler->imginfo.new_buff[y][x];
                handler->imginfo.new_buff[y][x] = handler->imginfo.new_buff[((handler->imginfo.new_height)-1) - y][x];
                handler->imginfo.new_buff[((handler->imginfo.new_height)-1) - y][x] = tmp;
            }
    else
        for (y = 0; y < handler->imginfo.new_height; y++)
            for (x = 0; x < handler->imginfo.new_width/2; x++)
            {
                pixel tmp = handler->imginfo.new_buff[y][x];
                handler->imginfo.new_buff[y][x] = handler->imginfo.new_buff[y][((handler->imginfo.new_width)-1)-x];
                handler->imginfo.new_buff[y][((handler->imginfo.new_width)-1) - x] = tmp;
            }
    return PPM_NOERROR;
}

int image_buff_alloc(pixel ***new_buff, unsigned int height, unsigned int width)
{
    int y;

    CHECK_ERROR((((*new_buff) = (pixel **) malloc(height * sizeof(pixel *))) == NULL), "can not allocate image buff for height\n")

    for (y = 0; y < height; y++)
    {
        CHECK_ERROR((((*new_buff)[y] = (pixel *) malloc(width * sizeof(pixel))) == NULL), "can not allocate image buff for width\n")
        memset((*new_buff)[y], 0x00, width * sizeof(pixel));
    }

    return PPM_NOERROR;
}

int mono(ppm_image_handler *handler) // TODO: should return error
{
    int x;
    int y;

    double matrix[] = {0.1250, 1.0000, 0.1875, 0.8125, 0.6250, 0.3750, 0.6875, 0.4375, 0.2500, 0.8750, 0.0625, 0.9375, 0.7500, 0.5000, 0.5625, 0.3125};

    handler->imginfo.file_type = FILETYPE_PBM;
    handler->imginfo.new_height = handler->imginfo.height;
    handler->imginfo.new_width = handler->imginfo.width;

    if (image_buff_alloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == PPM_ERROR) return PPM_ERROR;

// bayer 4x4  
// ------------------------------------------------------------------------------------------------------------------------------------------------------
    for (y = 0; y < handler->imginfo.new_height; y++)
        for (x = 0; x < handler->imginfo.new_width; x++)
        {
            unsigned char oldpixel = (handler->imginfo.buff[y][x].r + handler->imginfo.buff[y][x].g + handler->imginfo.buff[y][x].b) / 3;
            if (oldpixel >= matrix[(x % 4) * 4 + (y % 4)] * 255) handler->imginfo.new_buff[y][x].r = 0;
            else handler->imginfo.new_buff[y][x].r = 1;
        }
// ------------------------------------------------------------------------------------------------------------------------------------------------------
    return PPM_NOERROR;
}

int gray(ppm_image_handler *handler) // TODO: should return error
{
    int x;
    int y;

    handler->imginfo.file_type = FILETYPE_PGM;
    handler->imginfo.new_height = handler->imginfo.height;
    handler->imginfo.new_width = handler->imginfo.width;

    if (image_buff_alloc(&handler->imginfo.new_buff, handler->imginfo.new_height, handler->imginfo.new_width) == PPM_ERROR) return PPM_ERROR;

    for (y = 0; y < handler->imginfo.new_height; y++)
        for (x = 0; x < handler->imginfo.new_width; x++)
            handler->imginfo.new_buff[y][x].r = (unsigned char)((handler->imginfo.buff[y][x].r + handler->imginfo.buff[y][x].g + handler->imginfo.buff[y][x].b) / 3);

    return PPM_NOERROR;
}

void renewBuffer(ppm_image_handler *handler)
{
    releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);
    handler->imginfo.buff = handler->imginfo.new_buff;
    handler->imginfo.height = handler->imginfo.new_height;
    handler->imginfo.width = handler->imginfo.new_width;
}

int doProcessPPM(ppm_image_handler *handler)
{
    int retsz;

    // initialize ppm_image_handler
    handler->filep = fopen(handler->filename, "rb");
    CHECK_ERROR((handler->filep == NULL), "error. can not open file\n")

    CHECK_ERROR((fseek(handler->filep , 0 , SEEK_END) < 0), "error. can not set file position in fseek.\n")
    
    // get file size and allocate buffer for the whole file
    handler->filesize = ftell(handler->filep);
    rewind(handler->filep);
    handler->file_buffer = (unsigned char*) malloc(sizeof(unsigned char) * handler->filesize);
    CHECK_ERROR((handler->file_buffer == NULL), "error. can not allocate memory\n")
    retsz = fread(handler->file_buffer, 1, handler->filesize, handler->filep);
    CHECK_ERROR((retsz != handler->filesize), "error in reading input file.\n")

    // initialize variables before parsing
    handler->index_buffer = 0;
    handler->tkn.current_char = '\n';

    // get header file information
    if (getImageInfo(handler) != 0)
    {
        free(handler->file_buffer);
        fclose(handler->filep);
        return PPM_ERROR;
    }
  
    handler->imginfo.new_buff = NULL;

    if (handler->arg_flag.resize_enable)
    {
        double **weights[2];
        int **indices[2];
        int weights_sz[2];
        int im_sz[2];
        double scale[2];
        int order[2] = {0,0};
        int x, y;
        int out_width;
        int dim;

        CHECK_ERROR(((int) (handler->imginfo.new_width = handler->output_width_size) < 1), "invalid option for new width\n")

        scale[1] = (double) ((double) handler->imginfo.new_width / handler->imginfo.width);
        handler->imginfo.new_height = ((double) handler->imginfo.height * scale[1]);
        scale[0] = (double) ((double) handler->imginfo.new_height / handler->imginfo.height);
        
        if (scale[0] < scale[1]) order[1] = 1;
        else order[0] = 1;

        if (calc_contributions(handler->imginfo.height, handler->imginfo.new_height, scale[0], 4.0, &weights[0], &indices[0], &weights_sz[0]) == PPM_ERROR) return PPM_ERROR;
        if (calc_contributions(handler->imginfo.width, handler->imginfo.new_width, scale[1], 4.0, &weights[1], &indices[1], &weights_sz[1]) == PPM_ERROR) return PPM_ERROR;

        im_sz[0] = handler->imginfo.new_height;
        im_sz[1] = handler->imginfo.new_width;

        out_width = handler->imginfo.new_width;
        dim = order[0];
        if ((imresize(handler, im_sz[dim], dim, weights[dim], indices[dim], weights_sz[dim])) == PPM_ERROR) return PPM_ERROR;
        releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);
        handler->imginfo.buff = handler->imginfo.new_buff;
        handler->imginfo.new_width = out_width;
        handler->imginfo.height = handler->imginfo.new_height;
        handler->imginfo.width = handler->imginfo.new_width;
        dim = order[1];
        if ((imresize(handler, im_sz[dim], dim, weights[dim], indices[dim], weights_sz[dim])) == PPM_ERROR) return PPM_ERROR;

        for (x = 0; x < 2; x++)
        {
            for (y = 0; y < im_sz[x]; y++)
            {
                free((weights[x])[y]);
                free((indices[x])[y]);
            }
            free(weights[x]);
            free(indices[x]);
        }
    }

    if (handler->arg_flag.rotate_enable)
    {
        if (handler->arg_flag.resize_enable) renewBuffer(handler);
        rotate(handler);
    }

    if (handler->arg_flag.gray_enable)
    {
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable) renewBuffer(handler);
        gray(handler);
    }
        
    if (handler->arg_flag.mono_enable)
    {
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable) renewBuffer(handler);
        mono(handler);
    }

    if (handler->arg_flag.flipv_enable)
    {
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable)  renewBuffer(handler);
        flip(handler,1);
    }
        
    if (handler->arg_flag.fliph_enable)
    {
        if (handler->arg_flag.resize_enable || handler->arg_flag.rotate_enable) renewBuffer(handler);
        flip(handler,0);
    }
        
    // write the processed image to file
    if (putImageToFile(handler) != 0)
    {
        free(handler->file_buffer);
        fclose(handler->filep);
        releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);
        return PPM_ERROR;
    }

    // buffer is reused for flipv and fliph
    if (!(handler->norotate || handler->arg_flag.flipv_enable || handler->arg_flag.fliph_enable)) releaseBuffer(&handler->imginfo.buff, handler->imginfo.height);

    // release allocated image buffer
    free(handler->file_buffer);
    fclose(handler->filep);
    return PPM_NOERROR;
}
