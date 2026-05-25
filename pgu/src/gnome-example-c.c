/* PURPOSE:  This program is meant to be an example
             of what GUI programs look like written
             with the GTK 4 / GNOME libraries.
*/

#include <gtk/gtk.h>

#define MY_APP_TITLE "Gnome Example Program"
#define MY_APP_ID "org.example.gnome_example"
#define MY_BUTTON_TEXT "I Want to Quit the Example Program"
#define MY_QUIT_QUESTION "Are you sure you want to quit?"

/* Called once the user picks Yes or No on the confirmation dialog. */
static void on_quit_dialog_response(GObject *source, GAsyncResult *result,
                                    gpointer user_data) {
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);
    GApplication *app = G_APPLICATION(user_data);

    GError *error = NULL;
    int button = gtk_alert_dialog_choose_finish(dialog, result, &error);
    if (error != NULL) {
        /* Dialog was dismissed (e.g. Esc) -- treat as "No". */
        g_error_free(error);
        return;
    }

    /* Button index 0 corresponds to "Yes" (the first button we passed). */
    if (button == 0) {
        g_application_quit(app);
    }
}

/* Show a modal Yes/No confirmation dialog. */
static void show_quit_dialog(GtkWindow *parent, GApplication *app) {
    const char *buttons[] = {"_Yes", "_No", NULL};

    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", MY_QUIT_QUESTION);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 1);
    gtk_alert_dialog_set_default_button(dialog, 0);

    gtk_alert_dialog_choose(dialog, parent, NULL, on_quit_dialog_response, app);

    g_object_unref(dialog);
}

/* "Quit" button clicked. */
static void on_button_clicked(GtkButton *button, gpointer user_data) {
    GApplication *app = G_APPLICATION(user_data);
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button)));
    show_quit_dialog(parent, app);
}

/* User clicked the window's [x] -- intercept and confirm. */
static gboolean on_close_request(GtkWindow *window, gpointer user_data) {
    GApplication *app = G_APPLICATION(user_data);
    show_quit_dialog(window, app);
    return TRUE; /* prevent the window from closing automatically */
}

/* Build the UI when the application activates. */
static void on_activate(GApplication *app, gpointer user_data) {
    GtkWidget *window = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(window), MY_APP_TITLE);
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 100);

    GtkWidget *button = gtk_button_new_with_label(MY_BUTTON_TEXT);
    gtk_window_set_child(GTK_WINDOW(window), button);

    g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), app);
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request),
                     app);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app =
        gtk_application_new(MY_APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
