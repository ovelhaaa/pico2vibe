import platform
import shutil
import subprocess
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk


def default_cli_path() -> str:
    here = Path(__file__).resolve()
    candidate = here.parents[2] / "build" / "desktop_tools" / "univibe_cli.exe"
    return str(candidate)


class App:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        root.title("Univibe Offline GUI")
        root.geometry("760x440")

        self.cli_path = tk.StringVar(value=default_cli_path())
        self.input_path = tk.StringVar()
        self.output_path = tk.StringVar(value=str(Path.cwd() / "output_univibe.wav"))
        self.mode = tk.StringVar(value="chorus")
        self.seed = tk.StringVar(value="1")
        self.rate = tk.StringVar(value="1.2")
        self.depth = tk.StringVar(value="0.6")
        self.width = tk.StringVar(value="0.8")
        self.feedback = tk.StringVar(value="0.45")
        self.mix = tk.StringVar(value="1.0")

        frm = ttk.Frame(root, padding=12)
        frm.pack(fill=tk.BOTH, expand=True)

        self._row_file(frm, 0, "CLI", self.cli_path, self._pick_cli)
        self._row_file(frm, 1, "Input", self.input_path, self._pick_input)
        self._row_file(frm, 2, "Output", self.output_path, self._pick_output)

        self._row_entry(frm, 3, "Mode", self.mode, combo=True, values=["chorus", "vibrato"])
        self._row_entry(frm, 4, "Seed", self.seed)
        self._row_entry(frm, 5, "Rate (Hz)", self.rate)
        self._row_entry(frm, 6, "Depth", self.depth)
        self._row_entry(frm, 7, "Width", self.width)
        self._row_entry(frm, 8, "Feedback", self.feedback)
        self._row_entry(frm, 9, "Mix", self.mix)

        btns = ttk.Frame(frm)
        btns.grid(row=10, column=0, columnspan=3, sticky="w", pady=(14, 8))

        ttk.Button(btns, text="Processar", command=self.process).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(btns, text="Ouvir saida", command=self.play_output).pack(side=tk.LEFT)

        self.log = tk.Text(frm, height=9)
        self.log.grid(row=11, column=0, columnspan=3, sticky="nsew")

        frm.grid_columnconfigure(1, weight=1)
        frm.grid_rowconfigure(11, weight=1)

    def _row_file(self, parent, row, label, var, cmd):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=(0, 10), pady=4)
        ttk.Entry(parent, textvariable=var).grid(row=row, column=1, sticky="ew", pady=4)
        ttk.Button(parent, text="...", command=cmd).grid(row=row, column=2, sticky="w", padx=(8, 0), pady=4)

    def _row_entry(self, parent, row, label, var, combo=False, values=None):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=(0, 10), pady=4)
        if combo:
            cb = ttk.Combobox(parent, textvariable=var, values=values, state="readonly")
            cb.grid(row=row, column=1, sticky="w", pady=4)
        else:
            ttk.Entry(parent, textvariable=var).grid(row=row, column=1, sticky="w", pady=4)

    def _pick_cli(self):
        p = filedialog.askopenfilename(title="Selecionar CLI", filetypes=[("Executavel", "*.exe"), ("Todos", "*.*")])
        if p:
            self.cli_path.set(p)

    def _pick_input(self):
        p = filedialog.askopenfilename(title="Selecionar audio", filetypes=[("Audio", "*.wav *.mp3"), ("Todos", "*.*")])
        if p:
            self.input_path.set(p)
            base = Path(p).stem + "_univibe.wav"
            self.output_path.set(str(Path(p).with_name(base)))

    def _pick_output(self):
        p = filedialog.asksaveasfilename(title="Salvar como", defaultextension=".wav", filetypes=[("WAV", "*.wav"), ("MP3", "*.mp3")])
        if p:
            self.output_path.set(p)

    def _append_log(self, text: str):
        self.log.insert(tk.END, text + "\n")
        self.log.see(tk.END)

    def process(self):
        cli = self.cli_path.get().strip()
        inp = self.input_path.get().strip()
        out = self.output_path.get().strip()

        if not Path(cli).exists():
            messagebox.showerror("Erro", "CLI nao encontrada. Compile primeiro o desktop_tools.")
            return
        if not Path(inp).exists():
            messagebox.showerror("Erro", "Arquivo de entrada nao encontrado.")
            return

        cmd = [
            cli,
            "--input", inp,
            "--output", out,
            "--mode", self.mode.get().strip(),
            "--seed", self.seed.get().strip(),
            "--rate", self.rate.get().strip(),
            "--depth", self.depth.get().strip(),
            "--width", self.width.get().strip(),
            "--feedback", self.feedback.get().strip(),
            "--mix", self.mix.get().strip(),
        ]

        self._append_log("$ " + " ".join(cmd))

        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=False)
            if result.stdout:
                self._append_log(result.stdout.strip())
            if result.stderr:
                self._append_log(result.stderr.strip())

            if result.returncode != 0:
                messagebox.showerror("Erro", f"Processamento falhou (code={result.returncode}).")
            else:
                messagebox.showinfo("OK", "Processamento concluido.")
        except Exception as exc:
            messagebox.showerror("Erro", str(exc))

    def play_output(self):
        out = Path(self.output_path.get().strip())
        if not out.exists():
            messagebox.showerror("Erro", "Arquivo de saida nao existe.")
            return

        ffplay = shutil.which("ffplay")
        if ffplay:
            subprocess.Popen([ffplay, "-nodisp", "-autoexit", str(out)])
            return

        if platform.system().lower() == "windows" and out.suffix.lower() == ".wav":
            import winsound
            winsound.PlaySound(str(out), winsound.SND_FILENAME | winsound.SND_ASYNC)
            return

        messagebox.showwarning("Aviso", "Sem ffplay no PATH e sem backend local compatível para este formato.")


def main() -> int:
    root = tk.Tk()
    App(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())