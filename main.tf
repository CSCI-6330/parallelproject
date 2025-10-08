locals {
  tags = {
    Project = var.project_name
    Owner   = "data-team"
  }
}

# --- VPC (via official module) ---
module "vpc" {
  source  = "terraform-aws-modules/vpc/aws"
  version = "~> 5.8"

  name = "${var.project_name}-vpc"
  cidr = var.vpc_cidr

  azs             = slice(data.aws_availability_zones.available.names, 0, var.az_count)
  public_subnets  = var.public_subnet_cidrs
  private_subnets = var.private_subnet_cidrs

  enable_nat_gateway   = true
  single_nat_gateway   = true
  enable_dns_hostnames = true
  enable_dns_support   = true

  tags = local.tags
}

data "aws_availability_zones" "available" {}

# --- S3 bucket for logs/data ---
resource "aws_s3_bucket" "data" {
  bucket = "${var.project_name}-data-${random_id.rand.hex}"
  force_destroy = true
  tags   = local.tags
}

resource "aws_s3_bucket_versioning" "data" {
  bucket = aws_s3_bucket.data.id
  versioning_configuration { status = "Enabled" }
}

resource "aws_s3_bucket_server_side_encryption_configuration" "data" {
  bucket = aws_s3_bucket.data.id
  rule {
    apply_server_side_encryption_by_default {
      sse_algorithm = "AES256"
    }
  }
}

resource "random_id" "rand" {
  byte_length = 3
}

# --- Security Groups ---
# MSK SG (brokers on 9092 for plaintext demo; tighten in real use)
resource "aws_security_group" "msk" {
  name        = "${var.project_name}-msk-sg"
  description = "MSK security group"
  vpc_id      = module.vpc.vpc_id
  tags        = local.tags
}

# Allow Kafka from EMR and bastion within VPC
resource "aws_security_group_rule" "msk_ingress_kafka" {
  type                     = "ingress"
  from_port                = 9092
  to_port                  = 9092
  protocol                 = "tcp"
  security_group_id        = aws_security_group.msk.id
  source_security_group_id = aws_security_group.emr.id
}

resource "aws_security_group_rule" "msk_egress_all" {
  type              = "egress"
  from_port         = 0
  to_port           = 0
  protocol          = "-1"
  security_group_id = aws_security_group.msk.id
  cidr_blocks       = ["0.0.0.0/0"]
}

# EMR SG
resource "aws_security_group" "emr" {
  name        = "${var.project_name}-emr-sg"
  description = "EMR security group"
  vpc_id      = module.vpc.vpc_id
  tags        = local.tags
}

resource "aws_security_group_rule" "emr_egress_all" {
  type              = "egress"
  from_port         = 0
  to_port           = 0
  protocol          = "-1"
  security_group_id = aws_security_group.emr.id
  cidr_blocks       = ["0.0.0.0/0"]
}

# Optional bastion SG
resource "aws_security_group" "bastion" {
  count       = var.enable_bastion ? 1 : 0
  name        = "${var.project_name}-bastion-sg"
  description = "Bastion security group"
  vpc_id      = module.vpc.vpc_id
  tags        = local.tags
}

resource "aws_security_group_rule" "bastion_ingress_ssh" {
  count                    = var.enable_bastion ? 1 : 0
  type                     = "ingress"
  from_port                = 22
  to_port                  = 22
  protocol                 = "tcp"
  security_group_id        = aws_security_group.bastion[0].id
  cidr_blocks              = [var.ssh_ingress_cidr]
}

resource "aws_security_group_rule" "bastion_egress_all" {
  count             = var.enable_bastion ? 1 : 0
  type              = "egress"
  from_port         = 0
  to_port           = 0
  protocol          = "-1"
  security_group_id = aws_security_group.bastion[0].id
  cidr_blocks       = ["0.0.0.0/0"]
}

# --- Bastion host (optional) ---
resource "aws_instance" "bastion" {
  count                       = var.enable_bastion ? 1 : 0
  ami                         = data.aws_ami.amazon_linux2.id
  instance_type               = "t3.micro"
  subnet_id                   = module.vpc.public_subnets[0]
  vpc_security_group_ids      = [aws_security_group.bastion[0].id]
  key_name                    = var.key_pair_name != "" ? var.key_pair_name : null
  associate_public_ip_address = true

  tags = merge(local.tags, { Name = "${var.project_name}-bastion" })
}

data "aws_ami" "amazon_linux2" {
  most_recent = true
  owners      = ["amazon"]
  filter { name = "name" values = ["amzn2-ami-hvm-*-x86_64-gp2"] }
}

# --- MSK Cluster ---
# Minimal config uses plaintext 9092 inside VPC (for demo). For prod, enable TLS/SASL.
resource "aws_msk_cluster" "this" {
  cluster_name           = "${var.project_name}-msk"
  kafka_version          = var.msk_kafka_version
  number_of_broker_nodes = var.msk_broker_count

  broker_node_group_info {
    instance_type   = var.msk_broker_instance_type
    client_subnets  = module.vpc.private_subnets
    security_groups = [aws_security_group.msk.id]
    storage_info {
      ebs_storage_info { volume_size = 100 }
    }
  }

  client_authentication {
    unauthenticated = true
  }

  encryption_info { encryption_in_transit { client_broker = "PLAINTEXT" in_cluster = true } }

  tags = local.tags
}

# --- IAM for EMR ---
data "aws_iam_policy" "emr_service" {
  arn = "arn:aws:iam::aws:policy/service-role/AmazonElasticMapReduceRole"
}
data "aws_iam_policy" "emr_ec2" {
  arn = "arn:aws:iam::aws:policy/service-role/AmazonElasticMapReduceforEC2Role"
}
data "aws_iam_policy" "s3_full" {
  arn = "arn:aws:iam::aws:policy/AmazonS3FullAccess"
}

resource "aws_iam_role" "emr_service_role" {
  name = "${var.project_name}-EMR_DefaultRole"
  assume_role_policy = data.aws_iam_policy_document.emr_service_assume.json
  tags = local.tags
}
data "aws_iam_policy_document" "emr_service_assume" {
  statement {
    effect = "Allow"
    principals { type = "Service" identifiers = ["elasticmapreduce.amazonaws.com"] }
    actions = ["sts:AssumeRole"]
  }
}
resource "aws_iam_role_policy_attachment" "emr_service_attach" {
  role       = aws_iam_role.emr_service_role.name
  policy_arn = data.aws_iam_policy.emr_service.arn
}

resource "aws_iam_role" "emr_ec2_role" {
  name = "${var.project_name}-EMR_EC2_DefaultRole"
  assume_role_policy = data.aws_iam_policy_document.emr_ec2_assume.json
  tags = local.tags
}
data "aws_iam_policy_document" "emr_ec2_assume" {
  statement {
    effect = "Allow"
    principals { type = "Service" identifiers = ["ec2.amazonaws.com"] }
    actions = ["sts:AssumeRole"]
  }
}
resource "aws_iam_role_policy_attachment" "emr_ec2_attach1" {
  role       = aws_iam_role.emr_ec2_role.name
  policy_arn = data.aws_iam_policy.emr_ec2.arn
}
resource "aws_iam_role_policy_attachment" "emr_ec2_attach2" {
  role       = aws_iam_role.emr_ec2_role.name
  policy_arn = data.aws_iam_policy.s3_full.arn
}
resource "aws_iam_instance_profile" "emr_ec2_profile" {
  name = "${var.project_name}-EMR_EC2_Profile"
  role = aws_iam_role.emr_ec2_role.name
}

# --- EMR cluster ---
resource "aws_emr_cluster" "this" {
  name          = "${var.project_name}-emr"
  release_label = var.emr_release
  applications  = ["Hadoop", "Spark"]

  service_role      = aws_iam_role.emr_service_role.name
  ec2_attributes {
    subnet_id                         = module.vpc.private_subnets[0]
    instance_profile                  = aws_iam_instance_profile.emr_ec2_profile.arn
    emr_managed_master_security_group = aws_security_group.emr.id
    emr_managed_slave_security_group  = aws_security_group.emr.id
  }

  log_uri = "s3://${aws_s3_bucket.data.id}/emr-logs/"

  master_instance_group {
    instance_type = var.emr_master_instance_type
    instance_count = 1
    name = "master"
    ebs_config {
      size                 = 64
      type                 = "gp3"
      volumes_per_instance = 1
    }
  }

  core_instance_group {
    instance_type = var.emr_core_instance_type
    instance_count = var.emr_core_instance_count
    name = "core"
    ebs_config {
      size                 = 128
      type                 = "gp3"
      volumes_per_instance = 1
    }
  }

  configurations_json = jsonencode([
    {
      "Classification" : "core-site",
      "Properties" : {
        "fs.s3a.path.style.access" : "true"
      }
    },
    {
      "Classification" : "spark",
      "Properties" : {
        "spark.executor.memoryOverhead" : "1024"
      }
    }
  ])

  bootstrap_action {
    name = "Prep home dirs"
    path = "s3://${aws_s3_bucket.data.id}/bootstrap/prepare.sh"
    args = []
  }

  visible_to_all_users = true
  keep_job_flow_alive_when_no_steps = true

  tags = local.tags

  depends_on = [aws_s3_bucket.data]
}

# Optional: upload a tiny bootstrap script placeholder (requires aws cli outside TF usually).
# For demo, just document you should upload 'prepare.sh' to the S3 bucket:
#   #!/bin/bash
#   set -euxo pipefail
#   echo "Bootstrap OK" > /tmp/bootstrap_ok

output "bootstrap_note" {
  value = "Upload s3://${aws_s3_bucket.data.id}/bootstrap/prepare.sh before cluster creation (or remove the bootstrap_action)."
}

