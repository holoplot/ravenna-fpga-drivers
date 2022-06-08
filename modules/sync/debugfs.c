// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/of.h>

#include <ravenna/version.h>

#include "main.h"

static int ra_sync_summary_show(struct seq_file *s, void *p)
{
	struct ra_sync_priv *priv = s->private;

	mutex_lock(&priv->mutex);

	seq_puts(s, "Ravenna sync status\n");
	seq_printf(s, "  Driver version: %s\n", ra_driver_version());
	seq_printf(s, "  Device name: %s\n", priv->misc.name);
	seq_printf(s, "  MCLK frequency: %lu\n", clk_get_rate(priv->mclk));

	mutex_unlock(&priv->mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sync_summary);

static void ra_sync_remove_debugfs(void *root)
{
	debugfs_remove_recursive(root);
}

int ra_sync_debugfs_init(struct ra_sync_priv *priv)
{
	int ret;

	priv->debugfs = debugfs_create_dir(priv->misc.name, NULL);
	if (IS_ERR(priv->debugfs))
		return PTR_ERR(priv->debugfs);

	ret = devm_add_action_or_reset(priv->dev, ra_sync_remove_debugfs,
				       priv->debugfs);
	if (ret < 0)
		return ret;

	debugfs_create_file("summary", 0444, priv->debugfs,
			    priv, &ra_sync_summary_fops);

	return 0;
}
