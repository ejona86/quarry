
#ifndef __quarry_marshal_MARSHAL_H__
#define __quarry_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:VOID (quarry-marshal.glist:27) */
#define quarry_marshal_VOID__VOID	g_cclosure_marshal_VOID__VOID

/* VOID:BOOLEAN (quarry-marshal.glist:28) */
#define quarry_marshal_VOID__BOOLEAN	g_cclosure_marshal_VOID__BOOLEAN

/* VOID:INT (quarry-marshal.glist:29) */
#define quarry_marshal_VOID__INT	g_cclosure_marshal_VOID__INT

/* VOID:POINTER (quarry-marshal.glist:30) */
#define quarry_marshal_VOID__POINTER	g_cclosure_marshal_VOID__POINTER

/* VOID:POINTER,INT (quarry-marshal.glist:31) */
extern void quarry_marshal_VOID__POINTER_INT (GClosure     *closure,
                                              GValue       *return_value,
                                              guint         n_param_values,
                                              const GValue *param_values,
                                              gpointer      invocation_hint,
                                              gpointer      marshal_data);

/* VOID:POINTER,POINTER (quarry-marshal.glist:32) */
extern void quarry_marshal_VOID__POINTER_POINTER (GClosure     *closure,
                                                  GValue       *return_value,
                                                  guint         n_param_values,
                                                  const GValue *param_values,
                                                  gpointer      invocation_hint,
                                                  gpointer      marshal_data);

/* VOID:OBJECT,OBJECT (quarry-marshal.glist:33) */
extern void quarry_marshal_VOID__OBJECT_OBJECT (GClosure     *closure,
                                                GValue       *return_value,
                                                guint         n_param_values,
                                                const GValue *param_values,
                                                gpointer      invocation_hint,
                                                gpointer      marshal_data);

/* BOOLEAN:POINTER (quarry-marshal.glist:35) */
extern void quarry_marshal_BOOLEAN__POINTER (GClosure     *closure,
                                             GValue       *return_value,
                                             guint         n_param_values,
                                             const GValue *param_values,
                                             gpointer      invocation_hint,
                                             gpointer      marshal_data);

/* INT:POINTER (quarry-marshal.glist:37) */
extern void quarry_marshal_INT__POINTER (GClosure     *closure,
                                         GValue       *return_value,
                                         guint         n_param_values,
                                         const GValue *param_values,
                                         gpointer      invocation_hint,
                                         gpointer      marshal_data);

G_END_DECLS

#endif /* __quarry_marshal_MARSHAL_H__ */

