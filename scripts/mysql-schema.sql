CREATE TABLE IF NOT EXISTS `config_info` (
                                             `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `data_id` varchar(255) NOT NULL,
    `group_id` varchar(128) DEFAULT NULL,
    `content` longtext NOT NULL,
    `md5` varchar(32) DEFAULT NULL,
    `gmt_create` datetime NOT NULL,
    `gmt_modified` datetime NOT NULL,
    `src_user` text,
    `src_ip` varchar(50) DEFAULT NULL,
    `app_name` varchar(128) DEFAULT NULL,
    `tenant_id` varchar(128) DEFAULT '',
    `c_desc` varchar(256) DEFAULT NULL,
    `c_use` varchar(64) DEFAULT NULL,
    `effect` varchar(64) DEFAULT NULL,
    `type` varchar(64) DEFAULT NULL,
    `c_schema` text,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_configinfo_datagrouptenant` (`data_id`,`group_id`,`tenant_id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `config_info_aggr` (
                                                  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `data_id` varchar(255) NOT NULL,
    `group_id` varchar(128) NOT NULL,
    `datum_id` varchar(255) NOT NULL,
    `content` longtext NOT NULL,
    `gmt_modified` datetime NOT NULL,
    `app_name` varchar(128) DEFAULT NULL,
    `tenant_id` varchar(128) DEFAULT '',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_config_datagrouptenantdatum` (`data_id`,`group_id`,`tenant_id`,`datum_id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `config_info_beta` (
                                                  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `data_id` varchar(255) NOT NULL,
    `group_id` varchar(128) NOT NULL,
    `content` longtext NOT NULL,
    `beta_ips` varchar(1024) DEFAULT NULL,
    `md5` varchar(32) DEFAULT NULL,
    `gmt_create` datetime NOT NULL,
    `gmt_modified` datetime NOT NULL,
    `src_user` text,
    `src_ip` varchar(50) DEFAULT NULL,
    `app_name` varchar(128) DEFAULT NULL,
    `tenant_id` varchar(128) DEFAULT '',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_configinfo_beta_datagrouptenant` (`data_id`,`group_id`,`tenant_id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `config_info_tag` (
                                                 `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `data_id` varchar(255) NOT NULL,
    `group_id` varchar(128) NOT NULL,
    `tenant_id` varchar(128) DEFAULT '',
    `tag_id` varchar(128) NOT NULL,
    `app_name` varchar(128) DEFAULT NULL,
    `content` longtext NOT NULL,
    `md5` varchar(32) DEFAULT NULL,
    `gmt_create` datetime NOT NULL,
    `gmt_modified` datetime NOT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_configtag_datagrouptenanttag` (`data_id`,`group_id`,`tenant_id`,`tag_id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `config_info_tag_relation` (
                                                          `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `tag_name` varchar(128) DEFAULT NULL,
    `tag_type` varchar(64) DEFAULT NULL,
    PRIMARY KEY (`id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `config_info_gray` (
                                                  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `data_id` varchar(255) NOT NULL,
    `group_id` varchar(128) NOT NULL,
    `tenant_id` varchar(128) DEFAULT '',
    `content` longtext NOT NULL,
    `tag` varchar(128) NOT NULL,
    `gmt_create` datetime NOT NULL,
    `gmt_modified` datetime NOT NULL,
    `app_name` varchar(128) DEFAULT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_configinfo_gray_datagrouptenanttag` (`data_id`,`group_id`,`tenant_id`,`tag`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `config_tags_relation` (
                                                      `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `tag_name` varchar(128) DEFAULT NULL,
    `tag_type` varchar(64) DEFAULT NULL,
    PRIMARY KEY (`id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `config_info_lock` (
                                                  `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `data_id` varchar(255) NOT NULL,
    `group_id` varchar(128) NOT NULL,
    `md5` varchar(32) DEFAULT NULL,
    `gmt_create` datetime NOT NULL,
    `gmt_modified` datetime NOT NULL,
    `tenant_id` varchar(128) DEFAULT '',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_configlock_datagrouptenant` (`data_id`,`group_id`,`tenant_id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `his_config_info` (
                                                 `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `data_id` varchar(255) NOT NULL,
    `group_id` varchar(128) NOT NULL,
    `app_name` varchar(128) DEFAULT NULL,
    `content` longtext NOT NULL,
    `md5` varchar(32) DEFAULT NULL,
    `gmt_create` datetime NOT NULL,
    `gmt_modified` datetime NOT NULL,
    `src_user` text,
    `src_ip` varchar(50) DEFAULT NULL,
    `op_type` char(10) DEFAULT NULL,
    `tenant_id` varchar(128) DEFAULT '',
    `nid` bigint(20) DEFAULT NULL,
    PRIMARY KEY (`id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `tenant_info` (
                                             `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `kp` varchar(128) NOT NULL,
    `tenant_id` varchar(128) DEFAULT '',
    `tenant_name` varchar(128) DEFAULT NULL,
    `tenant_desc` varchar(256) DEFAULT NULL,
    `create_source` varchar(32) DEFAULT NULL,
    `gmt_create` bigint(20) NOT NULL,
    `gmt_modified` bigint(20) NOT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_tenantinfo_kptenantid` (`kp`,`tenant_id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `tenant_capacity` (
                                                 `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `tenant_id` varchar(128) DEFAULT '',
    `quota` int(10) unsigned NOT NULL DEFAULT '0',
    `usage` int(10) unsigned NOT NULL DEFAULT '0',
    `max_size` int(10) unsigned NOT NULL DEFAULT '0',
    `max_aggr_count` int(10) unsigned NOT NULL DEFAULT '0',
    `max_aggr_size` int(10) unsigned NOT NULL DEFAULT '0',
    `gmt_create` datetime NOT NULL,
    `gmt_modified` datetime NOT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_tenantcapacity_tenantid` (`tenant_id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `users` (
                                       `username` varchar(50) NOT NULL,
    `password` varchar(500) NOT NULL,
    `enabled` tinyint(1) NOT NULL,
    PRIMARY KEY (`username`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `roles` (
                                       `username` varchar(50) NOT NULL,
    `role` varchar(50) NOT NULL,
    UNIQUE KEY `idx_user_role` (`username`,`role`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `permissions` (
                                             `role` varchar(50) NOT NULL,
    `resource` varchar(255) NOT NULL,
    `action` varchar(8) NOT NULL,
    UNIQUE KEY `uk_role_permission` (`role`,`resource`,`action`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `group_capacity` (
    `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `group_id` varchar(128) NOT NULL,
    `quota` int(10) unsigned NOT NULL DEFAULT '0',
    `usage` int(10) unsigned NOT NULL DEFAULT '0',
    `max_size` int(10) unsigned NOT NULL DEFAULT '0',
    `max_aggr_count` int(10) unsigned NOT NULL DEFAULT '0',
    `max_aggr_size` int(10) unsigned NOT NULL DEFAULT '0',
    `gmt_create` datetime NOT NULL,
    `gmt_modified` datetime NOT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_groupcapacity_groupid` (`group_id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `config_history` (
    `nid` bigint(20) unsigned NOT NULL AUTO_INCREMENT COMMENT 'id',
    `id` bigint(20) unsigned NOT NULL COMMENT 'config id',
    `data_id` varchar(255) NOT NULL,
    `group_id` varchar(128) NOT NULL,
    `tenant_id` varchar(128) DEFAULT '',
    `app_name` varchar(128) DEFAULT NULL COMMENT 'app name',
    `content` longtext NOT NULL COMMENT 'content',
    `md5` varchar(32) DEFAULT NULL,
    `src_user` text,
    `src_ip` varchar(50) DEFAULT NULL,
    `op_type` char(10) DEFAULT NULL,
    `gmt_create` datetime NOT NULL,
    `gmt_modified` datetime NOT NULL,
    PRIMARY KEY (`nid`),
    KEY `idx_gmt_create` (`gmt_create`),
    KEY `idx_gmt_modified` (`gmt_modified`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='config history';

CREATE TABLE IF NOT EXISTS `config_info_listener` (
    `id` bigint(20) unsigned NOT NULL AUTO_INCREMENT,
    `data_id` varchar(255) NOT NULL,
    `group_id` varchar(128) NOT NULL,
    `tenant_id` varchar(128) DEFAULT '',
    `md5` varchar(32) DEFAULT NULL,
    `content` longtext,
    `gmt_create` datetime NOT NULL,
    `gmt_modified` datetime NOT NULL,
    `app_name` varchar(128) DEFAULT NULL,
    PRIMARY KEY (`id`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8;
