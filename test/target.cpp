#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <unistd.h>

struct Player {
    int32_t hp;
    int32_t maxHp;
    int32_t atk;
    float speed;
    char name[32];
};

// 全局指针 → 模拟游戏中的对象管理器
struct GameManager {
    Player* player;
    int level;
};

GameManager* g_manager = nullptr;  // 全局静态指针, 指针链可以找到

void takeDamage(Player* p, int dmg) {
    p->hp -= dmg;
    if (p->hp < 0) p->hp = 0;
    printf("  [hit] %s HP: %d/%d  (--%d)\n", p->name, p->hp, p->maxHp, dmg);
}

void heal(Player* p, int amount) {
    p->hp += amount;
    if (p->hp > p->maxHp) p->hp = p->maxHp;
    printf("  [heal] %s HP: %d/%d  (++%d)\n", p->name, p->hp, p->maxHp, amount);
}

int main() {
    setbuf(stdout, nullptr);

    // 堆上分配, 模拟游戏对象
    auto* player = new Player{
        .hp = 1000,
        .maxHp = 1000,
        .atk = 50,
        .speed = 3.14f,
        .name = "Hero"
    };

    g_manager = new GameManager{ .player = player, .level = 1 };

    printf("\n  === Test Target ===\n");
    printf("  PID:         %d\n", getpid());
    printf("  g_manager:   %p  (global ptr)\n", (void*)&g_manager);
    printf("  manager:     %p\n", (void*)g_manager);
    printf("  player:      %p\n", (void*)player);
    printf("  &hp:         %p\n", (void*)&player->hp);
    printf("  takeDamage:  %p\n", (void*)&takeDamage);
    printf("  heal:        %p\n", (void*)&heal);
    printf("\n  chain: g_manager -> player -> hp\n");
    printf("  1=hit  2=heal  3=status  0=exit\n\n");

    int choice;
    while (player->hp > 0) {
        printf("  > ");
        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n');
            continue;
        }
        switch (choice) {
            case 1: takeDamage(player, 75); break;
            case 2: heal(player, 30); break;
            case 3:
                printf("  [status] %s  HP:%d/%d  ATK:%d  SPD:%.2f  LV:%d\n",
                       player->name, player->hp, player->maxHp,
                       player->atk, player->speed, g_manager->level);
                break;
            case 0: goto done;
            default: printf("  1/2/3/0\n"); break;
        }
    }
    printf("\n  [dead] %s\n\n", player->name);
done:
    delete g_manager;
    delete player;
    return 0;
}
