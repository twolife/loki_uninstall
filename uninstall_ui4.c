
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <libintl.h>
#define _(String) gettext (String)

#include <gtk/gtk.h>

#include "setupdb.h"
#include "uninstall.h"
#include "uninstall_ui.h"

static int uninstall_cancelled = 0;
static GtkApplication *application = NULL;

static GtkWidget *main_window = NULL;
static GtkWidget *uninstall_stack = NULL;
static GtkWidget *uninstall_vbox = NULL;
static GtkWidget *recovered_space_label = NULL;
static GtkWidget *uninstall_button = NULL;
static GtkWidget *cancel_button = NULL;
static GtkWidget *finished_button = NULL;
static GtkWidget *uninstall_status_label = NULL;
static GtkWidget *uninstall_progress = NULL;

static GPtrArray *component_buttons = NULL;

/* List of open products that need closing */
struct product_list {
    product_t *product;
    struct product_list *next;
} *product_list = NULL;

static void add_product(product_t *product)
{
    struct product_list *entry;

    entry = (struct product_list *)malloc(sizeof *entry);
    if ( entry ) {
        entry->product = product;
        entry->next = product_list;
        product_list = entry;
    }
}

static void remove_product(product_t *product)
{
    struct product_list *prev, *entry;

    prev = NULL;
    for ( entry = product_list; entry; entry = entry->next ) {
        if ( entry->product == product ) {
            if ( prev ) {
                prev->next = entry->next;
            } else {
                product_list = entry->next;
            }
            free(entry);
            break;
        }
        prev = entry;
    }
}

static void close_products()
{
    struct product_list *freeable;

    while ( product_list ) {
        freeable = product_list;
        product_list = product_list->next;
        loki_closeproduct(freeable->product);
        free(freeable);
    }
}

/* List of components and associated widgets */
typedef struct {
    product_t *product;
    product_info_t *info;
    product_component_t *component;
    size_t size;
    struct component_button {
        GtkWidget *widget;
        struct component_button *next;
    } *buttons;
} component_list;

static component_list *create_component_list(product_t *product,
                                             product_info_t *info,
                                             product_component_t *component)
{
    component_list *list;

    list = (component_list *)malloc(sizeof *list);
    if ( list ) {
        list->product = product;
        list->info = info;
        list->component = component;
        list->size = loki_getsize_component(component);
        list->buttons = NULL;
    }
    return(list);
}

static void add_component_list(component_list *list, GtkWidget *widget)
{
    struct component_button *entry;

    if ( list ) {
        entry = (struct component_button *)malloc(sizeof *entry);
        if ( entry ) {
            entry->widget = widget;
            entry->next = list->buttons;
            list->buttons = entry;
        }
    }
}

static void process_events(void)
{
    GMainContext *context;

    context = g_main_context_default();
    while ( g_main_context_pending(context) ) {
        g_main_context_iteration(context, FALSE);
    }
}

static size_t calculate_recovered_space(void)
{
    gboolean ready;
    size_t size;
    guint i;

    ready = FALSE;
    size = 0;

    if ( component_buttons ) {
        for ( i = 0; i < component_buttons->len; ++i ) {
            GtkWidget *button = g_ptr_array_index(component_buttons, i);
            component_list *component;

            if ( gtk_check_button_get_active(GTK_CHECK_BUTTON(button)) ) {
                component = (component_list *)g_object_get_data(G_OBJECT(button), "data");
                if ( component ) {
                    size += component->size / 1024;
                    ready = TRUE;
                }
            }
        }
    }

    if ( recovered_space_label ) {
        char text[128];
        snprintf(text, sizeof(text), _("%d MB"), (int)(size/1024));
        gtk_label_set_text(GTK_LABEL(recovered_space_label), text);
    }
    if ( uninstall_button ) {
        gtk_widget_set_sensitive(uninstall_button, ready);
    }
    return(size);
}

void main_quit_slot(GtkWidget* w, gpointer data)
{
    uninstall_cancelled = 1;
    if ( application ) {
        g_application_quit(G_APPLICATION(application));
    }
}

static gboolean main_close_request_slot(GtkWindow *window, gpointer data)
{
    main_quit_slot(GTK_WIDGET(window), data);
    return TRUE;
}

void main_signal_abort(int status)
{
    main_quit_slot(NULL, NULL);
}

void cancel_uninstall_slot(GtkWidget *w, gpointer data)
{
    uninstall_cancelled = 1;
}

static void set_status_text(const char *text)
{
    if ( uninstall_status_label ) {
        gtk_label_set_text(GTK_LABEL(uninstall_status_label), text);
    }
}

void perform_uninstall_slot(GtkWidget* w, gpointer data)
{
    guint i;
    size_t size, total;
    char text[1024];

    /* First switch to the progress page */
    if ( uninstall_stack ) {
        gtk_stack_set_visible_child_name(GTK_STACK(uninstall_stack), "progress");
    }
    if ( finished_button ) {
        gtk_widget_set_sensitive(finished_button, FALSE);
    }

    /* Now uninstall all the selected components */
    size = 0;
    total = calculate_recovered_space();

    /* First do the addon components */
    if ( component_buttons ) {
        for ( i = 0; i < component_buttons->len && ! uninstall_cancelled; ++i ) {
            GtkWidget *button = g_ptr_array_index(component_buttons, i);
            component_list *component;

            if ( ! gtk_check_button_get_active(GTK_CHECK_BUTTON(button)) ) {
                continue;
            }

            component = (component_list *)g_object_get_data(G_OBJECT(button), "data");
            if ( ! component || loki_isdefault_component(component->component) ) {
                continue;
            }

            /* Put up the status */
            snprintf(text, sizeof(text), "%s: %s",
                    component->info->description,
                    loki_getname_component(component->component));
            set_status_text(text);

            /* See if the user wants to cancel the uninstall */
            process_events();

            /* Remove the component */
            uninstall_component(component->component, component->info);

            /* Update the progress bar */
            if ( total && uninstall_progress ) {
                size += component->size/1024;
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(uninstall_progress),
                                              (double)size/(double)total);
            }
            gtk_check_button_set_active(GTK_CHECK_BUTTON(button), FALSE);

            /* See if the user wants to cancel the uninstall */
            process_events();
        }
    }

    /* Now do the primary/default components */
    if ( component_buttons ) {
        for ( i = 0; i < component_buttons->len && ! uninstall_cancelled; ++i ) {
            GtkWidget *button = g_ptr_array_index(component_buttons, i);
            component_list *component;

            if ( ! gtk_check_button_get_active(GTK_CHECK_BUTTON(button)) ) {
                continue;
            }

            component = (component_list *)g_object_get_data(G_OBJECT(button), "data");
            if ( ! component || ! loki_isdefault_component(component->component) ) {
                continue;
            }

            /* Put up the status */
            snprintf(text, sizeof(text), "%s", component->info->description);
            set_status_text(text);

            /* See if the user wants to cancel the uninstall */
            process_events();

            /* Remove the component */
            perform_uninstall(component->product, component->info);
            remove_product(component->product);

            /* Update the progress bar */
            if ( total && uninstall_progress ) {
                size += component->size/1024;
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(uninstall_progress),
                                              (double)size/(double)total);
            }

            /* See if the user wants to cancel the uninstall */
            process_events();
        }
    }

    if ( uninstall_cancelled ) {
        set_status_text(_("Uninstall cancelled"));
    } else {
        set_status_text(_("Uninstall complete"));
    }
    if ( cancel_button ) {
        gtk_widget_set_sensitive(cancel_button, FALSE);
    }
    if ( finished_button ) {
        gtk_widget_set_sensitive(finished_button, TRUE);
    }
}

static void component_toggled_slot(GtkWidget* w, gpointer data)
{
    static int in_component_toggled_slot = 0;
    component_list *list = (component_list *)data;
    gboolean state;
    struct component_button *button;

    /* Prevent recursion */
    if ( in_component_toggled_slot ) {
        return;
    }
    in_component_toggled_slot = 1;

    /* Set the state for any linked components */
    state = gtk_check_button_get_active(GTK_CHECK_BUTTON(w));
    if ( list ) {
        for ( button = list->buttons; button; button = button->next ) {
            gtk_check_button_set_active(GTK_CHECK_BUTTON(button->widget), state);
            gtk_widget_set_sensitive(button->widget, !state);
        }
    }

    /* Calculate recovered space, and we're done */
    calculate_recovered_space();
    in_component_toggled_slot = 0;
}

static GtkWidget *create_ui(GtkApplication *app)
{
    GtkWidget *window;
    GtkWidget *main_box;
    GtkWidget *title;
    GtkWidget *stack;
    GtkWidget *selection_box;
    GtkWidget *scrolled;
    GtkWidget *summary_box;
    GtkWidget *button_box;
    GtkWidget *progress_box;
    GtkWidget *label;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), _("Loki Uninstall Tool"));
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 380);
    g_signal_connect(window, "close-request", G_CALLBACK(main_close_request_slot), NULL);

    main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(main_box, 12);
    gtk_widget_set_margin_bottom(main_box, 12);
    gtk_widget_set_margin_start(main_box, 12);
    gtk_widget_set_margin_end(main_box, 12);
    gtk_window_set_child(GTK_WINDOW(window), main_box);

    title = gtk_label_new(_("Select products and components to uninstall"));
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(main_box), title);

    stack = gtk_stack_new();
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_box_append(GTK_BOX(main_box), stack);
    uninstall_stack = stack;

    selection_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_stack_add_named(GTK_STACK(stack), selection_box, "selection");

    scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_box_append(GTK_BOX(selection_box), scrolled);

    uninstall_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), uninstall_vbox);

    summary_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(selection_box), summary_box);

    label = gtk_label_new(_("Disk space to recover:"));
    gtk_box_append(GTK_BOX(summary_box), label);

    recovered_space_label = gtk_label_new(_("0 MB"));
    gtk_box_append(GTK_BOX(summary_box), recovered_space_label);

    button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(selection_box), button_box);

    cancel_button = gtk_button_new_with_label(_("Exit"));
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(main_quit_slot), NULL);
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    uninstall_button = gtk_button_new_with_label(_("Uninstall"));
    gtk_widget_set_sensitive(uninstall_button, FALSE);
    g_signal_connect(uninstall_button, "clicked",
                     G_CALLBACK(perform_uninstall_slot), NULL);
    gtk_box_append(GTK_BOX(button_box), uninstall_button);

    progress_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_stack_add_named(GTK_STACK(stack), progress_box, "progress");

    uninstall_status_label = gtk_label_new(_("Preparing uninstall..."));
    gtk_widget_set_halign(uninstall_status_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(progress_box), uninstall_status_label);

    uninstall_progress = gtk_progress_bar_new();
    gtk_box_append(GTK_BOX(progress_box), uninstall_progress);

    button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(progress_box), button_box);

    cancel_button = gtk_button_new_with_label(_("Cancel"));
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(cancel_uninstall_slot), NULL);
    gtk_box_append(GTK_BOX(button_box), cancel_button);

    finished_button = gtk_button_new_with_label(_("Finished"));
    g_signal_connect(finished_button, "clicked", G_CALLBACK(main_quit_slot), NULL);
    gtk_box_append(GTK_BOX(button_box), finished_button);

    gtk_stack_set_visible_child_name(GTK_STACK(stack), "selection");
    return window;
}

static void application_activate(GtkApplication *app, gpointer user_data)
{
    GtkWidget *frame;
    GtkWidget *vbox;
    GtkWidget *button;
    GtkWidget *label;
    const char *product_name;
    product_t *product;
    product_info_t *product_info;
    product_component_t *component;
    component_list *component_list, *addon_list;
    char text[1024];

    uninstall_cancelled = 0;
    component_buttons = g_ptr_array_new();
    main_window = create_ui(app);

    /* Add emergency signal handlers */
    signal(SIGHUP, main_signal_abort);
    signal(SIGINT, main_signal_abort);
    signal(SIGQUIT, main_signal_abort);
    signal(SIGTERM, main_signal_abort);

    /* Fill in the list of products and components */
    for ( product_name=loki_getfirstproduct();
          product_name;
          product_name=loki_getnextproduct() ) {
        /* See if we can open the product */
        product = loki_openproduct(product_name);
        if ( ! product ) {
            continue;
        }
        /* See if we have permissions to remove the product */
        product_info = loki_getinfo_product(product);
        if ( ! check_permissions(product_info, 0) ) {
            loki_closeproduct(product);
            continue;
        }
        /* Add the product and components to our list */
        snprintf(text, sizeof(text), "%s", product_info->description);
        frame = gtk_frame_new(text);
        gtk_widget_set_margin_top(frame, 4);
        gtk_widget_set_margin_bottom(frame, 4);
        gtk_widget_set_margin_start(frame, 4);
        gtk_widget_set_margin_end(frame, 4);
        gtk_box_append(GTK_BOX(uninstall_vbox), frame);

        vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_frame_set_child(GTK_FRAME(frame), vbox);

        component = loki_getdefault_component(product);
        component_list = NULL;
        if ( component ) {
            component_list = create_component_list(product, product_info,
                                                   component);
            button = gtk_check_button_new_with_label(_("Complete uninstall"));
            gtk_box_append(GTK_BOX(vbox), button);
            g_signal_connect(button, "toggled",
                             G_CALLBACK(component_toggled_slot),
                             (gpointer)component_list);
            g_object_set_data(G_OBJECT(button), "data",
                              (gpointer)component_list);
            g_ptr_array_add(component_buttons, button);
        }
        for ( component = loki_getfirst_component(product);
              component;
              component = loki_getnext_component(component) ) {
            if ( loki_isdefault_component(component) ) {
                continue;
            }
            addon_list = create_component_list(product, product_info,
                                               component);
            button = gtk_check_button_new_with_label(loki_getname_component(component));
            gtk_box_append(GTK_BOX(vbox), button);
            g_signal_connect(button, "toggled",
                             G_CALLBACK(component_toggled_slot),
                             (gpointer)addon_list);
            g_object_set_data(G_OBJECT(button), "data",
                              (gpointer)addon_list);
            g_ptr_array_add(component_buttons, button);
            add_component_list(component_list, button);
        }

        /* Add this product to our list of open products */
        add_product(product);
    }

    /* Check to make sure there's something to uninstall */
    if ( ! product_list ) {
        label = gtk_label_new(
            _("No products found.\nAre you the one that installed the software?")
        );
        gtk_box_append(GTK_BOX(uninstall_vbox), label);
    }

    /* Run the UI. */
    gtk_window_present(GTK_WINDOW(main_window));
}

/* Run a GUI to select and uninstall products */
int uninstall_ui(int argc, char *argv[])
{
    int status;

    application = gtk_application_new("com.lokigames.loki_uninstall", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(application_activate), NULL);

    status = g_application_run(G_APPLICATION(application), argc, argv);

    /* Close all the products and return */
    close_products();
    if ( component_buttons ) {
        g_ptr_array_free(component_buttons, TRUE);
        component_buttons = NULL;
    }
    g_clear_object(&application);
    return status;
}
