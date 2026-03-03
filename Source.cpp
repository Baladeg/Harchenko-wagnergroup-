#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <cstdio> // для std::remove

class BinaryFile : public std::fstream {
private:
    static const int HEADER_SIZE = 3 * sizeof(int64_t); // Размерность, количество, адрес массива
    int64_t capacity; // размерность массива
    int64_t count;    // текущее количество указателей
    int64_t ptr_array_addr; // смещение массива указателей

    // Вспомогательная функция: получить строку по смещению
    std::string read_string_at(int64_t offset) {
        this->seekg(offset);
        int32_t len;
        this->read(reinterpret_cast<char*>(&len), sizeof(len));
        if (len < 0 || len > 1000000) {
            throw std::runtime_error("Неверная длина строки");
        }
        std::vector<char> buffer(len);
        this->read(buffer.data(), len);
        return std::string(buffer.data(), len);
    }

    // Вспомогательная функция: записать строку в конец файла
    int64_t write_string_at_end(const std::string& str) {
        this->seekp(0, std::ios::end);
        int64_t offset = this->tellp();
        int32_t len = static_cast<int32_t>(str.size());
        this->write(reinterpret_cast<const char*>(&len), sizeof(len));
        this->write(str.c_str(), len);
        return offset;
    }

    // Обновить заголовок файла
    void update_header() {
        this->seekp(0);
        this->write(reinterpret_cast<const char*>(&capacity), sizeof(capacity));
        this->write(reinterpret_cast<const char*>(&count), sizeof(count));
        this->write(reinterpret_cast<const char*>(&ptr_array_addr), sizeof(ptr_array_addr));
    }

public:
    // Конструктор: открывает файл, читает заголовок или создает новый
    BinaryFile(const std::string& filename, bool create_new = false) : std::fstream(filename, std::ios::binary | std::ios::in | std::ios::out) {
        if (create_new || !this->is_open()) {
            this->open(filename, std::ios::binary | std::ios::out | std::ios::trunc);
            capacity = 10;
            count = 0;
            ptr_array_addr = HEADER_SIZE + capacity * sizeof(int64_t);
            this->seekp(0);
            update_header();
            this->seekp(ptr_array_addr);
            for (int64_t i = 0; i < capacity; ++i) {
                int64_t dummy = 0;
                this->write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
            }
            this->close();
            this->open(filename, std::ios::binary | std::ios::in | std::ios::out);
        }

        this->seekg(0);
        this->read(reinterpret_cast<char*>(&capacity), sizeof(capacity));
        this->read(reinterpret_cast<char*>(&count), sizeof(count));
        this->read(reinterpret_cast<char*>(&ptr_array_addr), sizeof(ptr_array_addr));

        // Защита от некорректных данных
        if (capacity <= 0 || capacity > 1000000) {
            throw std::runtime_error("Некорректный размер массива указателей");
        }
        if (count < 0 || count > capacity) {
            throw std::runtime_error("Некорректное количество элементов");
        }
        if (ptr_array_addr < HEADER_SIZE) {
            throw std::runtime_error("Некорректный адрес массива указателей");
        }
    }

    // Оператор сложения: добавить строку, поддерживая порядок
    BinaryFile& operator+(const std::string* str_ptr) {
        if (!str_ptr) return *this;

        std::string new_str = *str_ptr;

        // Считываем все текущие строки и их смещения
        std::vector<std::pair<std::string, int64_t>> current_strings;
        for (int64_t i = 0; i < count; ++i) {
            this->seekg(ptr_array_addr + i * sizeof(int64_t));
            int64_t offset;
            this->read(reinterpret_cast<char*>(&offset), sizeof(offset));
            std::string s = read_string_at(offset);
            current_strings.emplace_back(s, offset);
        }

        // Вставляем новую строку в нужное место
        auto it = std::lower_bound(current_strings.begin(), current_strings.end(), new_str,
            [](const std::pair<std::string, int64_t>& a, const std::string& b) {
                return a.first < b;
            });

        // Записываем новую строку в конец файла
        int64_t new_offset = write_string_at_end(new_str);

        // Если массив полон, увеличиваем его
        if (count >= capacity) {
            int64_t old_capacity = capacity;
            capacity *= 2;

            // Перемещаем массив указателей в конец файла
            int64_t old_ptr_array_addr = ptr_array_addr;
            ptr_array_addr = HEADER_SIZE + capacity * sizeof(int64_t);

            // Обновляем заголовок
            update_header();

            // Собираем новые указатели
            std::vector<int64_t> new_pointers;
            size_t insert_pos = it - current_strings.begin();
            for (size_t i = 0; i < current_strings.size(); ++i) {
                new_pointers.push_back(current_strings[i].second);
                if (i == insert_pos) {
                    new_pointers.push_back(new_offset);
                }
            }
            if (insert_pos == current_strings.size()) {
                new_pointers.push_back(new_offset);
            }

            // Записываем обновленный массив указателей
            this->seekp(ptr_array_addr);
            for (int64_t ptr : new_pointers) {
                this->write(reinterpret_cast<const char*>(&ptr), sizeof(ptr));
            }

            ++count;
            update_header();
        }
        else {
            // Вставляем смещение новой строки в нужное место
            std::vector<int64_t> temp_pointers(count + 1);
            int64_t index = it - current_strings.begin();
            for (int64_t i = 0; i < index; ++i)
                temp_pointers[i] = current_strings[i].second;
            temp_pointers[index] = new_offset;
            for (int64_t i = index; i < count; ++i)
                temp_pointers[i + 1] = current_strings[i].second;

            this->seekp(ptr_array_addr);
            for (int64_t ptr : temp_pointers) {
                this->write(reinterpret_cast<const char*>(&ptr), sizeof(ptr));
            }

            ++count;
            update_header();
        }

        return *this;
    }

    // Вспомогательный метод для отладки: печать всех строк
    void print_all() {
        for (int64_t i = 0; i < count; ++i) {
            this->seekg(ptr_array_addr + i * sizeof(int64_t));
            int64_t offset;
            this->read(reinterpret_cast<char*>(&offset), sizeof(offset));
            std::string s = read_string_at(offset);
            std::cout << s << std::endl;
        }
    }
};

// Точка входа
int main() {
    try {
        // Удаляем старый файл, если существует
        std::remove("test.dat");

        BinaryFile file("test.dat", true); // Создаём новый файл

        std::string str1 = "death";
        std::string str2 = "lives";
        std::string str3 = "banan";
        std::string str4 = "Apelsinka";

        file + &str1;
        file + &str2;
        file + &str3;
        file + &str4;

        std::cout << "Che V faile:" << std::endl;
        file.print_all();

    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << std::endl;
    }

    return 0;
}