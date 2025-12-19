import sys
import cv2
import numpy as np
import tkinter as tk
from tkinter import ttk, filedialog
from PIL import Image, ImageTk
import time

# ============================
# CONFIGURATION
# ============================

USE_REMAP = True  # switch OpenCV method here

DEBOUNCE_MS = 2000
MODE = "CONTROLLED"
SCALE = 0.4

# je de paramètres à peu près ok pour "image2.jpg"
# MODE = "CONTROLLED"
# SCALE = 0.5
# f = 1212
# k1 = -0.162
# k2 = 0.05

# je de paramètres à peu près ok pour "image2.jpg"
# MODE = "CONTROLLED"
# SCALE = 1.0
# f = 727
# k1 = 0.081
# k2 = 0.05

# je de paramètres affinés pour "image2.jpg"
# MODE = "CONTROLLED"
# SCALE = 1.0
# f = 725
# k1 = 0.09
# k2 = 0.05


# je de paramètres affinés pour "image2.jpg"
# MODE = "CONTROLLED"
# SCALE = 0.5
# f = 960
# k1 = 0.0
# k2 = 0.0

SHOW_GUIDES = True
GUIDE_COLOR = (0, 255, 0)   # vert
GUIDE_THICKNESS = 4

# ============================
# UTILITIES
# ============================
def draw_guides(img):
    """
    Dessine des lignes guides verticales pour l'aide à la calibration.
    """
    h, w = img.shape[:2]

    print("[GUIDES] Drawing guide lines")

    x_center = w // 2
    x_left = int(w * 0.25)
    x_right = int(w * 0.75)

    # Ligne verticale centrale
    cv2.line(img, (x_center, 0), (x_center, h),
             GUIDE_COLOR, GUIDE_THICKNESS)

    # Lignes verticales latérales
    cv2.line(img, (x_left, 0), (x_left, h),
             GUIDE_COLOR, GUIDE_THICKNESS)

    cv2.line(img, (x_right, 0), (x_right, h),
             GUIDE_COLOR, GUIDE_THICKNESS)

    return img


def resize_for_display(img, max_w, max_h):
    h, w = img.shape[:2]
    scale = min(max_w / w, max_h / h)
    new_size = (int(w * scale), int(h * scale))
    return cv2.resize(img, new_size, interpolation=cv2.INTER_AREA)

# ============================
# MAIN APP
# ============================

class DewarpApp(tk.Tk):
    def __init__(self, image_path):
        super().__init__()

        print("[INIT] Starting Dewarp GUI")

        self.title("Fisheye Dewarp Calibration Tool")
        self.geometry("1400x700")

        print("[UI] Forcing window visibility")
        self.update_idletasks()
        self.deiconify()
        self.lift()
        self.attributes("-topmost", True)
        self.after(100, lambda: self.attributes("-topmost", False))
        self.focus_force()

        self.original = cv2.imread(image_path)
        if self.original is None:
            raise RuntimeError("Unable to load image")

        self.H, self.W = self.original.shape[:2]
        print(f"[INFO] Image loaded: {self.W}x{self.H}")

        # Default params
        self.params = {
            "f": tk.DoubleVar(value=self.W / 2),
            "cx": tk.DoubleVar(value=self.W / 2),
            "cy": tk.DoubleVar(value=self.H / 2),
            "k1": tk.DoubleVar(value=-0.25),
            "k2": tk.DoubleVar(value=0.05),
            "k3": tk.DoubleVar(value=0.0),
            "k4": tk.DoubleVar(value=0.0),
        }

        self.last_params_snapshot = self.snapshot_params()
        self.debounce_job = None

        self._build_ui()
        self.update_idletasks()
        # Lancer le premier rendu APRÈS que la fenêtre soit affichée
        self.after(0, lambda: self._update_images(initial=True))

    # ------------------------

    def snapshot_params(self):
        return {k: v.get() for k, v in self.params.items()}

    def params_changed(self):
        current = self.snapshot_params()
        changed = current != self.last_params_snapshot
        print(f"[DEBUG] Params changed: {changed}")
        return changed

    # ------------------------

    def _build_ui(self):
        print("[UI] Adding test label")


        # Image frames
        img_frame = ttk.Frame(self)
        img_frame.pack(fill="both", expand=True)

        self.left_label = tk.Label(img_frame)
        self.left_label.pack(side="left", expand=True)

        self.right_label = tk.Label(img_frame)
        self.right_label.pack(side="left", expand=True)

        # Controls
        ctrl = ttk.Frame(self)
        ctrl.pack(fill="x")

        self.status = ttk.Label(ctrl, text="Ready")
        self.status.pack(side="right", padx=10)

        self.advanced = tk.BooleanVar(value=False)

        for name in ["f", "k1", "k2"]:
            self._add_slider(ctrl, name)

        ttk.Checkbutton(
            ctrl, text="Advanced",
            variable=self.advanced,
            command=self._toggle_advanced
        ).pack(side="left", padx=10)

        self.advanced_frame = ttk.Frame(ctrl)

        for name in ["cx", "cy", "k3", "k4"]:
            self._add_slider(self.advanced_frame, name)

        ttk.Button(ctrl, text="Save Dewarped", command=self._save).pack(side="left", padx=10)

    # ------------------------

    def _add_slider(self, parent, name):
        frame = ttk.Frame(parent)
        frame.pack(side="left", padx=6)

        ttk.Label(frame, text=name).pack()

        var = self.params[name]

        # --- Définition des bornes ---
        if name.startswith("k"):
            vmin, vmax, step = -1.0, 1.0, 0.001
        elif name in ("cx", "cy"):
            vmin, vmax, step = 0, self.W if name == "cx" else self.H, 1
        else:  # f
            vmin, vmax, step = 200, self.W * 2, 1

        # --- Slider ---
        slider = ttk.Scale(
            frame,
            from_=vmin,
            to=vmax,
            variable=var,
            command=lambda e: self._on_param_change()
        )
        slider.pack(fill="x")

        # --- Spinbox (↑ ↓) ---
        spin = tk.Spinbox(
            frame,
            from_=vmin,
            to=vmax,
            increment=step,
            textvariable=var,
            width=8,
            command=self._on_param_change
        )
        spin.pack()

        # --- Entry (saisie directe) ---
        entry = ttk.Entry(frame, width=8, textvariable=var)
        entry.pack()

        # --- Validation à la sortie du champ ---
        entry.bind("<Return>", lambda e: self._on_param_change())
        entry.bind("<FocusOut>", lambda e: self._on_param_change())

    # ------------------------

    def _toggle_advanced(self):
        if self.advanced.get():
            self.advanced_frame.pack(fill="x")
        else:
            self.advanced_frame.pack_forget()

    # ------------------------

    def _on_param_change(self):
        if self.debounce_job:
            self.after_cancel(self.debounce_job)

        self.debounce_job = self.after(DEBOUNCE_MS, self._debounced_update)

    # ------------------------

    def _debounced_update(self):
        if not self.params_changed():
            print("[INFO] No parameter change detected, skipping recompute")
            return

        print("[INFO] Recomputing dewarp")
        self.status.config(text="Recalcul en cours…")
        self.update_idletasks()

        self._update_images()

        self.last_params_snapshot = self.snapshot_params()
        self.status.config(text="Ready")

    # ------------------------

    def _compute_dewarp(self):
        # --- Lecture des paramètres UI ---
        f = self.params["f"].get()
        cx = self.params["cx"].get()
        cy = self.params["cy"].get()
        k1 = self.params["k1"].get()
        k2 = self.params["k2"].get()
        k3 = self.params["k3"].get()
        k4 = self.params["k4"].get()

        print(f"[PARAMS] f={f:.2f} cx={cx:.2f} cy={cy:.2f} "
              f"k=({k1:.4f},{k2:.4f},{k3:.4f},{k4:.4f})")

        # --- Matrice intrinsèque d’entrée ---
        K = np.array([
            [f, 0.0, cx],
            [0.0, f, cy],
            [0.0, 0.0, 1.0]
        ], dtype=np.float32)

        D = np.array([k1, k2, k3, k4], dtype=np.float32)

        # --- Matrice de sortie (K_out) ---
        if MODE == "LEGACY":
            K_out = K
            print("[INFO] MODE=LEGACY (exact legacy behavior)")
        elif MODE == "CONTROLLED":
            K_out = K.copy()
            K_out[0, 0] *= SCALE
            K_out[1, 1] *= SCALE
            print(f"[INFO] MODE=CONTROLLED (SCALE={SCALE:.3f})")
        else:
            raise ValueError("Invalid MODE")

        print("[DEBUG] K_out:")
        print(K_out)

        # --- Construction des maps ---
        map1, map2 = cv2.fisheye.initUndistortRectifyMap(
            K, D,
            np.eye(3, dtype=np.float32),
            K_out,
            (self.W, self.H),
            cv2.CV_16SC2
        )

        # --- Remap ---
        dewarped = cv2.remap(
            self.original,
            map1,
            map2,
            interpolation=cv2.INTER_LINEAR,
            borderMode=cv2.BORDER_CONSTANT
        )

        if SHOW_GUIDES:
            dewarped = draw_guides(dewarped)

        return dewarped

    # ------------------------

    def _update_images(self, initial=False):
        print("[RENDER] Updating images")

        if initial:
            dewarped = self._compute_dewarp()
        else:
            dewarped = self._compute_dewarp()

        self.dewarped = dewarped

        max_w = self.winfo_width() // 2 - 20
        max_h = self.winfo_height() - 200

        left = resize_for_display(self.original, max_w, max_h)
        right = resize_for_display(dewarped, max_w, max_h)

        self.left_imgtk = ImageTk.PhotoImage(Image.fromarray(cv2.cvtColor(left, cv2.COLOR_BGR2RGB)))
        self.right_imgtk = ImageTk.PhotoImage(Image.fromarray(cv2.cvtColor(right, cv2.COLOR_BGR2RGB)))

        self.left_label.config(image=self.left_imgtk)
        self.right_label.config(image=self.right_imgtk)

    # ------------------------

    def _save(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".jpg",
            filetypes=[("JPEG", "*.jpg")]
        )
        if path:
            cv2.imwrite(path, self.dewarped)
            print(f"[INFO] Dewarped image saved to {path}")

# ============================
# ENTRY POINT
# ============================

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python dewarp_gui.py <image_path>")
        sys.exit(1)

    app = DewarpApp(sys.argv[1])
    print("[MAIN] Entering Tk mainloop")
    app.mainloop()
