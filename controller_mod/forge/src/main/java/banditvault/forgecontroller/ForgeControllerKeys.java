package banditvault.forgecontroller;

import net.minecraft.client.Options;
import net.minecraft.client.KeyMapping;

final class ForgeControllerKeys {
    final KeyMapping forward;
    final KeyMapping back;
    final KeyMapping left;
    final KeyMapping right;
    final KeyMapping jump;
    final KeyMapping sneak;
    final KeyMapping sprint;
    final KeyMapping attack;
    final KeyMapping use;
    final KeyMapping inventory;
    final KeyMapping swapOffhand;
    final KeyMapping drop;
    final KeyMapping pickItem;

    ForgeControllerKeys(Options options) {
        forward = options.f_92085_;
        back = options.f_92087_;
        left = options.f_92086_;
        right = options.f_92088_;
        jump = options.f_92089_;
        sneak = options.f_92090_;
        sprint = options.f_92091_;
        attack = options.f_92096_;
        use = options.f_92095_;
        inventory = options.f_92092_;
        swapOffhand = options.f_92093_;
        drop = options.f_92094_;
        pickItem = options.f_92097_;
    }
}
