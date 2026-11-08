#ifndef MKSC_PORTABLE_DRIVER_AFFINE_H
#define MKSC_PORTABLE_DRIVER_AFFINE_H
#include <stddef.h>
#include <stdint.h>
enum { PORTABLE_DRIVER_AFFINE_TABLES=5, PORTABLE_DRIVER_AFFINE_ROWS=32 };
typedef struct {
    uint16_t inverse_x_q8,inverse_y_q8;
    uint8_t duration;
} PortableDriverAffineRow;
typedef struct {
    uint8_t mode,count;
    const PortableDriverAffineRow *rows;
} PortableDriverAffineTable;
typedef struct { PortableDriverAffineTable tables[PORTABLE_DRIVER_AFFINE_TABLES]; } PortableDriverAffineRules;
typedef struct {
    uint16_t inverse_x_q8,inverse_y_q8;
    uint8_t mode,row,remaining,pending_mode;
} PortableDriverAffine;
/* D214's +70 channel. Coefficients are inverse scales, independent of the
 * palette/burn channel at +72. Sentinel rows are omitted from native tables. */
static inline const PortableDriverAffineTable *portable_driver_affine_table(
    const PortableDriverAffineRules *rules,unsigned mode)
{
    if(rules==NULL)return NULL;
    for(unsigned i=0u;i<PORTABLE_DRIVER_AFFINE_TABLES;++i)
        if(rules->tables[i].mode==mode)return &rules->tables[i];
    return NULL;
}
/* Return 1 accepted, 0 ignored (including legacy missing rules), -1 invalid. */
static inline int portable_driver_affine_request(PortableDriverAffine *s,
    const PortableDriverAffineRules *rules,unsigned mode)
{
    if(s==NULL)return -1;
    if(rules==NULL)return 0;
    /* FD7C can queue a crush before dependencies are available. Its channel
     * takes priority over later landing/lightning-expiry requests that turn. */
    if(s->pending_mode){
        const unsigned pending=s->pending_mode;
        s->pending_mode=0u;
        if(pending!=5u)return -1;
        const PortableDriverAffineTable *p=portable_driver_affine_table(rules,5u);
        if(p==NULL||p->rows==NULL||p->count==0u||p->count>PORTABLE_DRIVER_AFFINE_ROWS||
           p->rows[0].duration==0u||p->rows[0].duration>127u)return -1;
        s->mode=5u;s->row=0u;s->remaining=p->rows[0].duration;
    }
    const PortableDriverAffineTable *t=portable_driver_affine_table(rules,mode);
    if(t==NULL||t->rows==NULL||t->count==0u||t->count>PORTABLE_DRIVER_AFFINE_ROWS||
       t->rows[0].duration==0u||t->rows[0].duration>127u)return -1;
    if((mode==1u&&(s->mode==1u||s->mode==5u||s->mode==6u))||
       (mode==4u&&(s->mode==1u||s->mode==4u||s->mode==5u||s->mode==6u))||
       (mode==7u&&(s->mode==1u||s->mode==4u||s->mode==5u||s->mode==7u)))return 0;
    s->mode=(uint8_t)mode;s->row=0u;s->remaining=t->rows[0].duration;
    return 1;
}
/* 49D78/D328 follows driver simulation. Write current coefficients before
 * decrementing/advancing, so a one-tick row is visible for exactly one tick.
 * Return 1 for B29C's local unfinished-driver cue133; caller owns that gate. */
static inline int portable_driver_affine_tick(PortableDriverAffine *s,
    const PortableDriverAffineRules *rules)
{
    if(s==NULL)return -1;
    if(rules==NULL)return 0;
    if(s->pending_mode){
        if(s->pending_mode!=5u)return -1;
        s->pending_mode=0u;
        if(portable_driver_affine_request(s,rules,5u)<0)return -1;
    }
    if(s->mode==0u)return 0;
    const PortableDriverAffineTable *t=portable_driver_affine_table(rules,s->mode);
    if(t==NULL||t->rows==NULL||t->count>PORTABLE_DRIVER_AFFINE_ROWS||s->row>=t->count||
       s->remaining==0u)return -1;
    const PortableDriverAffineRow *r=&t->rows[s->row];
    if(r->inverse_x_q8==0u||r->inverse_y_q8==0u||r->duration==0u||r->duration>127u)return -1;
    s->inverse_x_q8=r->inverse_x_q8;s->inverse_y_q8=r->inverse_y_q8;
    if(--s->remaining)return 0;
    const int cue=s->mode==7u&&s->row==0u;
    if(++s->row<t->count){s->remaining=t->rows[s->row].duration;return cue;}
    if(s->mode==5u){
        if(portable_driver_affine_request(s,rules,6u)<0)return -1;
    }else{s->mode=0u;s->row=0u;}
    return cue;
}
#endif
