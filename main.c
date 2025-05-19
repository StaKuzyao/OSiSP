#define _GNU_SOURCE
#include <ncursesw/ncurses.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_PATH_LEN 4096
#define MAX_FILES 1024
#define MAX_LINE_LEN 256

typedef struct {
    char name[MAX_LINE_LEN];
    char full_path[MAX_PATH_LEN];
    int is_dir;
    off_t size;
    time_t mtime;
} FileEntry;

typedef struct {
    FileEntry files[MAX_FILES];
    int count;
    int selected;
    int top;
    char path[MAX_PATH_LEN];
} Panel;

Panel left_panel, right_panel;
Panel *active_panel;
int running = 1;
int show_hidden = 0;
char clipboard[MAX_PATH_LEN] = "";
int clipboard_type = 0; // 0 - empty, 1 - copy, 2 - cut

void init_panel(Panel *panel, const char *path) {
    strncpy(panel->path, path, MAX_PATH_LEN);
    panel->count = 0;
    panel->selected = 0;
    panel->top = 0;
}

void scan_directory(Panel *panel) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char full_path[MAX_PATH_LEN];

    panel->count = 0;

    if ((dir = opendir(panel->path)) == NULL) {
        return;
    }

    // Добавляем родительский каталог
    if (strcmp(panel->path, "/") != 0) {
        strcpy(panel->files[panel->count].name, "..");
        snprintf(panel->files[panel->count].full_path, MAX_PATH_LEN, "%s/..", panel->path);
        panel->files[panel->count].is_dir = 1;
        panel->files[panel->count].size = 0;
        panel->files[panel->count].mtime = 0;
        panel->count++;
    }

    while ((entry = readdir(dir)) != NULL && panel->count < MAX_FILES) {
        if (!show_hidden && entry->d_name[0] == '.') {
            continue;
        }

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(full_path, MAX_PATH_LEN, "%s/%s", panel->path, entry->d_name);

        if (stat(full_path, &file_stat) == -1) {
            continue;
        }

        strncpy(panel->files[panel->count].name, entry->d_name, MAX_LINE_LEN);
        strncpy(panel->files[panel->count].full_path, full_path, MAX_PATH_LEN);
        panel->files[panel->count].is_dir = S_ISDIR(file_stat.st_mode);
        panel->files[panel->count].size = file_stat.st_size;
        panel->files[panel->count].mtime = file_stat.st_mtime;
        panel->count++;
    }

    closedir(dir);
}



void draw_panel(WINDOW *win, Panel *panel, int is_active) {
    int i, y, maxy, maxx;
    getmaxyx(win, maxy, maxx);

    werase(win);
    box(win, 0, 0);

    // Заголовок панели
    wattron(win, A_BOLD);
    mvwprintw(win, 0, 2, " %s ", panel->path);
    wattroff(win, A_BOLD);

    // Содержимое панели
    for (i = panel->top, y = 1; i < panel->count && y < maxy - 2; i++, y++) {
        if (i == panel->selected) {
            wattron(win, A_REVERSE);
        }

        if (panel->files[i].is_dir) {
            wattron(win, COLOR_PAIR(1));
            mvwaddwstr(win, y, 2, L"📁 ");
            wattroff(win, COLOR_PAIR(1));
        } else {
            mvwaddwstr(win, y, 2, L"📄 ");
        }

        mvwprintw(win, y, 5, "%-*.*s", maxx - 25, maxx - 25, panel->files[i].name);

        if (!panel->files[i].is_dir) {
            mvwprintw(win, y, maxx - 20, "%10ld", panel->files[i].size);
        }

        if (i == panel->selected) {
            wattroff(win, A_REVERSE);
        }
    }

    // Статусная строка
    wattron(win, A_BOLD);
    mvwprintw(win, maxy - 2, 2, "%d/%d", panel->selected + 1, panel->count);
    wattroff(win, A_BOLD);

    if (is_active) {
        wattron(win, COLOR_PAIR(2));
        box(win, 0, 0);
        wattroff(win, COLOR_PAIR(2));
    }

    wrefresh(win);
}

void draw_status_bar(WINDOW *win) {
    int maxx;
    getmaxx(win);

    werase(win);
    
    wattron(win, A_BOLD);
    mvwprintw(win, 0, 0, " F1:Помощь");
    wattroff(win, A_BOLD);
    
    wrefresh(win);
}

void show_help() {
    WINDOW *help_win = newwin(LINES - 4, COLS - 21, 2, 10);
    keypad(help_win, TRUE);
    box(help_win, 0, 0);
    
    mvwprintw(help_win, 1, 2, "Клавиши управления:");
    mvwprintw(help_win, 3, 2, "Стрелки - Навигация по файлам");
    mvwprintw(help_win, 4, 2, "Enter    - Открыть файл/каталог");
    mvwprintw(help_win, 5, 2, "Tab      - Переключение между панелями");
    mvwprintw(help_win, 6, 2, "F3       - Просмотр файла");
    mvwprintw(help_win, 7, 2, "F4       - Редактировать файл (vim)");
    mvwprintw(help_win, 8, 2, "F5       - Копировать файл/каталог");
    mvwprintw(help_win, 9, 2, "F6       - Переименовать файл/каталог");
    mvwprintw(help_win, 10, 2, "F7       - Создать каталог");
    mvwprintw(help_win, 11, 2, "F8       - Удалить файл/каталог");
    mvwprintw(help_win, 12, 2, "F9       - Показать/скрыть скрытые файлы");
    mvwprintw(help_win, 13, 2, "F10      - Выход");
    mvwprintw(help_win, 14, 2, "F12      - Вставить");
    
    mvwprintw(help_win, 16, 2, "Нажмите любую клавишу для продолжения...");
    wrefresh(help_win);
    
    wgetch(help_win);
    delwin(help_win);
}

void create_directory() {
    echo();
    curs_set(1);
    
    WINDOW *input_win = newwin(3, COLS - 20, LINES/2 - 1, 10);
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 1, "Имя нового каталога: ");
    wrefresh(input_win);
    
    char dirname[MAX_LINE_LEN];
    wgetnstr(input_win, dirname, MAX_LINE_LEN - 1);
    
    noecho();
    curs_set(0);
    delwin(input_win);
    
    if (strlen(dirname) > 0) {
        char path[MAX_PATH_LEN];
        snprintf(path, MAX_PATH_LEN, "%s/%s", active_panel->path, dirname);
        
        if (mkdir(path, 0755) == -1) {
            // Ошибка создания каталога
            WINDOW *err_win = newwin(3, COLS - 20, LINES/2 - 1, 10);
            box(err_win, 0, 0);
            mvwprintw(err_win, 1, 1, "Ошибка: не удалось создать каталог!");
            wrefresh(err_win);
            wgetch(err_win);
            delwin(err_win);
        } else {
            scan_directory(active_panel);
        }
    }
}


void delete_file() {
    if (active_panel->selected >= active_panel->count) return;
    
    FileEntry *entry = &active_panel->files[active_panel->selected];
    
    WINDOW *confirm_win = newwin(5, COLS - 20, LINES/2 - 2, 10);
    box(confirm_win, 0, 0);
    mvwprintw(confirm_win, 1, 1, "Удалить '%s'?", entry->name);
    mvwprintw(confirm_win, 2, 1, "Y - Да, N - Нет");
    wrefresh(confirm_win);
    
    int ch = wgetch(confirm_win);
    delwin(confirm_win);
    
    if (ch == 'y' || ch == 'Y') {
        if (entry->is_dir) {
            if (rmdir(entry->full_path) == -1) {
                // Ошибка удаления каталога
                WINDOW *err_win = newwin(3, COLS - 20, LINES/2 - 1, 10);
                box(err_win, 0, 0);
                mvwprintw(err_win, 1, 1, "Ошибка: не удалось удалить каталог!");
                wrefresh(err_win);
                wgetch(err_win);
                delwin(err_win);
                return;
            }
        } else {
            if (unlink(entry->full_path) == -1) {
                // Ошибка удаления файла
                WINDOW *err_win = newwin(3, COLS - 20, LINES/2 - 1, 10);
                box(err_win, 0, 0);
                mvwprintw(err_win, 1, 1, "Ошибка: не удалось удалить файл!");
                wrefresh(err_win);
                wgetch(err_win);
                delwin(err_win);
                return;
            }
        }
        scan_directory(active_panel);
    }
}

void copy_to_clipboard(int cut) {
    if (active_panel->selected >= active_panel->count) return;
    
    FileEntry *entry = &active_panel->files[active_panel->selected];
    strncpy(clipboard, entry->full_path, MAX_PATH_LEN);
    clipboard_type = cut ? 2 : 1;
}

void paste_from_clipboard() {
    if (clipboard_type == 0) return;
    
    FileEntry *entry = &active_panel->files[active_panel->selected];
    char *src = clipboard;
    char dest[MAX_PATH_LEN];
    snprintf(dest, MAX_PATH_LEN, "%s/%s", active_panel->path, basename(src));
    
    if (strcmp(src, dest) == 0) {
        // Нельзя копировать/перемещать в то же место
        return;
    }
    
    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
        if (clipboard_type == 1) { // Копирование
            execlp("cp", "cp", "-r", src, dest, NULL);
        } else { // Перемещение
            execlp("mv", "mv", src, dest, NULL);
        }
        exit(1);
    } else if (pid > 0) {
        // Родительский процесс
        wait(NULL);
        if (clipboard_type == 2) { // После перемещения обновляем исходную панель
            Panel *other_panel = (active_panel == &left_panel) ? &right_panel : &left_panel;
            if (strstr(src, other_panel->path) == src) {
                scan_directory(other_panel);
            }
        }
        scan_directory(active_panel);
        clipboard_type = 0;
    }
}

void rename_file() {
    if (active_panel->selected >= active_panel->count) return;
    
    FileEntry *entry = &active_panel->files[active_panel->selected];
    
    echo();
    curs_set(1);
    
    WINDOW *input_win = newwin(3, COLS - 20, LINES/2 - 1, 10);
    box(input_win, 0, 0);
    mvwprintw(input_win, 1, 1, "Новое имя: ");
    mvwprintw(input_win, 1, 12, entry->name);
    wmove(input_win, 1, 12);
    wrefresh(input_win);
    
    char newname[MAX_LINE_LEN];
    wgetnstr(input_win, newname, MAX_LINE_LEN - 1);
    
    noecho();
    curs_set(0);
    delwin(input_win);
    
    if (strlen(newname) > 0) {
        char newpath[MAX_PATH_LEN];
        snprintf(newpath, MAX_PATH_LEN, "%s/%s", active_panel->path, newname);
        
        if (rename(entry->full_path, newpath) == -1) {
            // Ошибка переименования
            WINDOW *err_win = newwin(3, COLS - 20, LINES/2 - 1, 10);
            box(err_win, 0, 0);
            mvwprintw(err_win, 1, 1, "Ошибка: не удалось переименовать!");
            wrefresh(err_win);
            wgetch(err_win);
            delwin(err_win);
        } else {
            scan_directory(active_panel);
        }
    }
}

void view_file() {
    if (active_panel->selected >= active_panel->count) return;
    
    FileEntry *entry = &active_panel->files[active_panel->selected];
    if (entry->is_dir) return;

    // Сохраняем текущее состояние терминала
    def_prog_mode();
    endwin();

    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
        execlp("less", "less", entry->full_path, NULL);
        // Если execlp вернул управление, значит произошла ошибка
        fprintf(stderr, "Ошибка запуска less: %s\n", strerror(errno));
        exit(1);
    } else if (pid > 0) {
        // Родительский процесс ждёт завершения просмотрщика
        int status;
        waitpid(pid, &status, 0);
        
        // Восстанавливаем состояние ncurses
        reset_prog_mode();
        refresh();
    } else {
        // Ошибка fork
        reset_prog_mode();
        refresh();
    }
}

void edit_file() {
    if (active_panel->selected >= active_panel->count) return;
    
    FileEntry *entry = &active_panel->files[active_panel->selected];
    if (entry->is_dir) return;

    // Сохраняем текущее состояние терминала
    def_prog_mode();
    endwin();

    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
        execlp("nvim", "nvim", entry->full_path, NULL);
        exit(1);
    } else if (pid > 0) {
        // Родительский процесс ждёт завершения редактора
        wait(NULL);
        
        // Восстанавливаем состояние ncurses
        reset_prog_mode();
        refresh();
        
        // Обновляем информацию о файле
        struct stat file_stat;
        if (stat(entry->full_path, &file_stat) == 0) {
            entry->size = file_stat.st_size;
            entry->mtime = file_stat.st_mtime;
        }
    }
}

void change_directory(Panel *panel, const char *path) {
    char new_path[MAX_PATH_LEN];
    char resolved_path[MAX_PATH_LEN];
    
    if (path[0] == '/') {
        strncpy(new_path, path, MAX_PATH_LEN);
    } else {
        snprintf(new_path, MAX_PATH_LEN, "%s/%s", panel->path, path);
    }
    
    if (realpath(new_path, resolved_path) == NULL) {
        return;
    }
    
    strncpy(panel->path, resolved_path, MAX_PATH_LEN);
    scan_directory(panel);
    panel->selected = 0;
    panel->top = 0;
}

void open_file() {
    if (active_panel->selected >= active_panel->count) return;
    
    FileEntry *entry = &active_panel->files[active_panel->selected];
    
    if (entry->is_dir) {
        change_directory(active_panel, entry->name);
        return;
    }

    // Сохраняем состояние терминала
    def_prog_mode();
    endwin();

    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
        const char *ext = strrchr(entry->name, '.');
        
        if (ext) {
            // Для известных типов файлов можно использовать специальные программы
            if (strcasecmp(ext, ".pdf") == 0) {
                execlp("xdg-open", "xdg-open", entry->full_path, NULL);
            } else if (strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".log") == 0) {
                execlp("less", "less", entry->full_path, NULL);
            } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".png") == 0 || 
                       strcasecmp(ext, ".gif") == 0) {
                execlp("xdg-open", "xdg-open", entry->full_path, NULL);
            }
        }
        
        // Универсальный способ открытия файла
        execlp("xdg-open", "xdg-open", entry->full_path, NULL);
        exit(1);
    } else if (pid > 0) {
        // Родительский процесс
        int status;
        waitpid(pid, &status, 0);
        
        // Восстанавливаем терминал
        reset_prog_mode();
        refresh();
    } else {
        // Ошибка fork
        reset_prog_mode();
        refresh();
    }
}

void handle_input() {
    int ch = getch();
    
    switch (ch) {
        case KEY_UP:
            if (active_panel->selected > 0) {
                active_panel->selected--;
                if (active_panel->selected < active_panel->top) {
                    active_panel->top = active_panel->selected;
                }
            }
            break;
            
        case KEY_DOWN:
            if (active_panel->selected < active_panel->count - 1) {
                active_panel->selected++;
                int maxy = getmaxy(stdscr) - 6;
                if (active_panel->selected >= active_panel->top + maxy) {
                    active_panel->top++;
                }
            }
            break;
            
        case KEY_LEFT:
        case KEY_RIGHT:
        case 9: // TAB
            active_panel = (active_panel == &left_panel) ? &right_panel : &left_panel;
            break;
            
        case 10: // ENTER
            open_file();
            break;
            
        case KEY_F(1):
            show_help();
            break;
            
        case KEY_F(3):
            view_file();
            break;
            
        case KEY_F(4):
            edit_file();
            scan_directory(active_panel);
            break;
            
        case KEY_F(5):
            copy_to_clipboard(0);
            break;
            
        case KEY_F(6):
            rename_file();
            break;
            
        case KEY_F(7):
            create_directory();
            break;
            
        case KEY_F(8):
            delete_file();
            break;
            
        case KEY_F(9):
            show_hidden = !show_hidden;
            scan_directory(&left_panel);
            scan_directory(&right_panel);
            break;
            
        case KEY_F(10):
            running = 0;
            break;
            
        case KEY_F(12): // F12 - вставить
            paste_from_clipboard();
            break;
    }
}

int main() {
    // Настройка локали для поддержки UTF-8 и русского языка
    setlocale(LC_ALL, "");
    
    // Инициализация ncurses
    initscr();
    start_color();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    
    // Инициализация цветов
    init_pair(1, COLOR_BLUE, COLOR_BLACK);    // Каталоги
    init_pair(2, COLOR_YELLOW, COLOR_BLACK);  // Активная панель
    
    // Получаем домашний каталог пользователя
    char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        home_dir = "/";
    }
    
    // Инициализация панелей
    init_panel(&left_panel, home_dir);
    init_panel(&right_panel, "/");
    active_panel = &left_panel;
    
    // Сканируем директории
    scan_directory(&left_panel);
    scan_directory(&right_panel);
    
    // Основной цикл
    while (running) {
        // Рассчитываем размеры панелей
        int maxy, maxx;
        getmaxyx(stdscr, maxy, maxx);
        int panel_width = (maxx - 4) / 2;
        
        // Создаем окна для панелей
        WINDOW *left_win = newwin(maxy - 3, panel_width, 1, 1);
        WINDOW *right_win = newwin(maxy - 3, panel_width, 1, panel_width + 2);
        WINDOW *status_win = newwin(1, maxx, maxy - 1, 0);
        
        // Рисуем панели
        draw_panel(left_win, &left_panel, active_panel == &left_panel);
        draw_panel(right_win, &right_panel, active_panel == &right_panel);
        draw_status_bar(status_win);
        
        // Освобождаем окна
        delwin(left_win);
        delwin(right_win);
        delwin(status_win);
        
        // Обработка ввода
        handle_input();
    }
    
    // Завершение работы ncurses
    endwin();
    
    return 0;
}