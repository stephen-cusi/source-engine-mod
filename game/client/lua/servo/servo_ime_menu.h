#ifndef HL2SB_SERVO_IME_MENU_H
#define HL2SB_SERVO_IME_MENU_H

#include <vgui/IInput.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Menu.h>
#include <tier1/strtools.h>

class HLSBServoIMEMenu {
public:
    explicit HLSBServoIMEMenu(vgui::Panel *owner) : owner_(owner), menu_(NULL) {}
    ~HLSBServoIMEMenu() { Hide(); }

    void Hide() {
        if (menu_) {
            menu_->SetVisible(false);
            delete menu_;
            menu_ = NULL;
        }
    }

    void Update(int x, int y) {
        Hide();
        vgui::IInput *input = vgui::input();
        const int count = input->GetCandidateListCount();
        const int size = input->GetCandidateListPageSize();
        const int selected = input->GetCandidateListSelectedItem();
        if (count <= 0 || size <= 0) return;
        int start = input->GetCandidateListPageStart();
        if (selected >= 0 && (selected < start || selected >= start + size)) {
            start = (selected / size) * size;
            input->SetCandidateListPageStart(start);
        }
        if (start < 0 || start >= count) start = 0;
        menu_ = new vgui::Menu(owner_, "ServoIMECandidates");
        menu_->SetKeyBoardInputEnabled(false);
        menu_->SetMouseInputEnabled(false);
        const int first = input->CandidateListStartsAtOne() ? 1 : 0;
        for (int i = start; i < count && i - start < size; ++i) {
            wchar_t candidate[256] = {0};
            wchar_t label[288] = {0};
            input->GetCandidate(i, candidate, sizeof(candidate));
            V_snwprintf(label, sizeof(label) / sizeof(label[0]), L"%d %ls", i - start + first, candidate);
            const int id = menu_->AddMenuItem("Candidate", label, (KeyValues *)NULL, owner_);
            if (i == selected) menu_->SetCurrentlyHighlightedItem(id);
        }
        menu_->SetVisible(true);
        menu_->InvalidateLayout(true);
        int menuWidth, menuHeight, screenWidth, screenHeight;
        menu_->GetSize(menuWidth, menuHeight);
        vgui::surface()->GetScreenSize(screenWidth, screenHeight);
        owner_->LocalToScreen(x, y);
        if (x + menuWidth > screenWidth) x = screenWidth - menuWidth;
        if (y + menuHeight > screenHeight) y -= menuHeight;
        menu_->SetPos(x > 0 ? x : 0, y > 0 ? y : 0);
    }

private:
    vgui::Panel *owner_;
    vgui::Menu *menu_;
    HLSBServoIMEMenu(const HLSBServoIMEMenu &);
    HLSBServoIMEMenu &operator=(const HLSBServoIMEMenu &);
};

#endif
