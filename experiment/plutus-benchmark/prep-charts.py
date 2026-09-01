# ---
# jupyter:
#   jupytext:
#     text_representation:
#       extension: .py
#       format_name: light
#       format_version: '1.5'
#       jupytext_version: 1.16.4
#   kernelspec:
#     display_name: Python 3 (ipykernel)
#     language: python
#     name: python3
# ---

# # Notebook format
#
# This is a Jupyter Notebook that has been converted to pure Python using Jupytext for easier version control.
#
# It can be converted back to the ipynb format using the following command:
# ```
# jupytext --to ipynb prep-results.py
# ```

# +
import pandas as pd
import os
import re

data_dir = './20260826'

def load_data(dir_path):
    recs = []
    for fname in os.listdir(dir_path):
        m = re.match("^([^\\-]+)-(\\d+).csv$", fname)
        if m:
            print(fname)
            df = pd.read_csv(dir_path + "/" + fname)
            num_workers = int(m.group(2))
            rate_per_worker = 1 / df["run_time"].mean()
            rate_total = rate_per_worker * num_workers
            recs.append({
                "name": m.group(1),
                "threads": num_workers,
                "rate_per_worker": round(rate_per_worker, 1),
                "rate_total": round(rate_total, 1),
            })
    return pd.DataFrame.from_records(recs)

df = load_data(data_dir)
df.head()
# -


# ## Plutus witness validation rate

# +
import seaborn as sns
import matplotlib.pyplot as plt
import numpy as np

sns.set_palette(['#2CA02C'], n_colors=100)
fig, ax = plt.subplots()
g = sns.barplot(data=df, y='rate_total', x='threads', hue='name', ax=ax)
g.set(ylabel='rate, plutus witnesses per second', xlabel='number of worker threads')
for c in g.containers:
    g.bar_label(c)
ax.legend()
fig.tight_layout()
fig.savefig(data_dir + '/chart-rate.png', dpi=300)
plt.show()
plt.close(fig)
# -
# ## Parallel efficiency

base_rate = df.loc[df['threads'] == 1]['rate_per_worker']
df['efficiency'] = df['rate_per_worker'] * 100 / base_rate[0]
fig, ax = plt.subplots()
g = sns.barplot(data=df, y='efficiency', x='threads', hue='name', saturation=1.0, ax=ax)
g.set(ylabel='parallel efficiency, %', xlabel='number of worker threads')
ax.axhline(90, ls='--', color='b', label='90% level')
ax.legend()
fig.tight_layout()
fig.savefig(data_dir + '/chart-efficiency.png', dpi=300)
plt.show()
plt.close(fig)

# # Predicted time to validate all Plutus witnesses

# +
# this count is produced by txwit-stat command
total_redeemers = 60_061_161

df['pred_time'] = round(total_redeemers / df['rate_total'] / 60, 1)
fig, ax = plt.subplots()
g = sns.barplot(data=df, y='pred_time', x='threads', hue='name', saturation=1.0, ax=ax)
g.set(ylabel='predicted time, min', xlabel='number of worker threads')
for c in g.containers:
    g.bar_label(c)
ax.legend()
fig.tight_layout()
fig.savefig(data_dir + '/chart-predicted-time.png', dpi=300)
plt.show()
plt.close(fig)
